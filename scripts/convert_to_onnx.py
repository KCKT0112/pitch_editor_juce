#!/usr/bin/env python3
"""
Convert PC-NSF-HiFiGAN model to ONNX format.
"""

import sys
import os
import json
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.nn import Conv1d, ConvTranspose1d
from torch.nn.utils import weight_norm, remove_weight_norm
import numpy as np

# Model constants
LRELU_SLOPE = 0.1


def init_weights(m, mean=0.0, std=0.01):
    classname = m.__class__.__name__
    if classname.find("Conv") != -1:
        m.weight.data.normal_(mean, std)


def get_padding(kernel_size, dilation=1):
    return int((kernel_size * dilation - dilation) / 2)


class ResBlock1(nn.Module):
    def __init__(self, channels, kernel_size=3, dilation=(1, 3, 5)):
        super(ResBlock1, self).__init__()
        self.convs1 = nn.ModuleList([
            weight_norm(Conv1d(channels, channels, kernel_size, 1, dilation=dilation[0],
                               padding=get_padding(kernel_size, dilation[0]))),
            weight_norm(Conv1d(channels, channels, kernel_size, 1, dilation=dilation[1],
                               padding=get_padding(kernel_size, dilation[1]))),
            weight_norm(Conv1d(channels, channels, kernel_size, 1, dilation=dilation[2],
                               padding=get_padding(kernel_size, dilation[2])))
        ])
        self.convs1.apply(init_weights)

        self.convs2 = nn.ModuleList([
            weight_norm(Conv1d(channels, channels, kernel_size, 1, dilation=1,
                               padding=get_padding(kernel_size, 1))),
            weight_norm(Conv1d(channels, channels, kernel_size, 1, dilation=1,
                               padding=get_padding(kernel_size, 1))),
            weight_norm(Conv1d(channels, channels, kernel_size, 1, dilation=1,
                               padding=get_padding(kernel_size, 1)))
        ])
        self.convs2.apply(init_weights)

    def forward(self, x):
        for c1, c2 in zip(self.convs1, self.convs2):
            xt = F.leaky_relu(x, LRELU_SLOPE)
            xt = c1(xt)
            xt = F.leaky_relu(xt, LRELU_SLOPE)
            xt = c2(xt)
            x = xt + x
        return x

    def remove_weight_norm(self):
        for l in self.convs1:
            remove_weight_norm(l)
        for l in self.convs2:
            remove_weight_norm(l)


class SineGen(nn.Module):
    """Sine wave generator for NSF."""
    
    def __init__(self, samp_rate, harmonic_num=0, sine_amp=0.1,
                 noise_std=0.003, voiced_threshold=0):
        super(SineGen, self).__init__()
        self.sine_amp = sine_amp
        self.noise_std = noise_std
        self.harmonic_num = harmonic_num
        self.sampling_rate = samp_rate
        self.voiced_threshold = voiced_threshold

    def forward(self, f0, upp):
        """
        Args:
            f0: [B, 1, T] - fundamental frequency in Hz
            upp: upsampling factor
        Returns:
            sine_waves: [B, 1, T*upp]
        """
        with torch.no_grad():
            # Upsample F0
            f0 = f0.unsqueeze(1)  # [B, 1, T]
            f0_up = F.interpolate(f0, scale_factor=upp, mode='nearest')  # [B, 1, T*upp]
            f0_up = f0_up.squeeze(1)  # [B, T*upp]
            
            # Generate phase
            rad = f0_up / self.sampling_rate  # Normalized frequency
            rad = rad % 1  # Wrap to [0, 1]
            
            # Cumulative sum for phase
            phase = torch.cumsum(rad, dim=1) * 2 * np.pi
            
            # Generate sine wave
            sine_wave = torch.sin(phase) * self.sine_amp
            
            # Apply voiced mask
            voiced_mask = (f0_up > self.voiced_threshold).float()
            sine_wave = sine_wave * voiced_mask
            
            # Add noise for unvoiced
            noise = torch.randn_like(sine_wave) * self.noise_std
            sine_wave = sine_wave + noise * (1 - voiced_mask)
            
            return sine_wave.unsqueeze(1)  # [B, 1, T*upp]


class SourceModuleHnNSF(nn.Module):
    """Source module for harmonic-plus-noise NSF."""
    
    def __init__(self, sampling_rate, harmonic_num=8, sine_amp=0.1,
                 add_noise_std=0.003, voiced_threshold=10):
        super(SourceModuleHnNSF, self).__init__()
        self.sine_amp = sine_amp
        self.noise_std = add_noise_std
        self.l_sin_gen = SineGen(sampling_rate, harmonic_num, sine_amp,
                                  add_noise_std, voiced_threshold)
        self.l_linear = nn.Linear(1, 1)
        self.l_tanh = nn.Tanh()

    def forward(self, x, upp):
        sine_wavs = self.l_sin_gen(x, upp)
        sine_wavs = self.l_linear(sine_wavs.transpose(1, 2)).transpose(1, 2)
        sine_wavs = self.l_tanh(sine_wavs)
        return sine_wavs


class Generator(nn.Module):
    """PC-NSF-HiFiGAN Generator."""
    
    def __init__(self, h):
        super(Generator, self).__init__()
        self.h = h
        self.num_kernels = len(h['resblock_kernel_sizes'])
        self.num_upsamples = len(h['upsample_rates'])
        
        # Calculate total upsampling factor
        self.upp = 1
        for r in h['upsample_rates']:
            self.upp *= r
        
        # Source module (NSF)
        self.m_source = SourceModuleHnNSF(
            sampling_rate=h['sampling_rate'],
            harmonic_num=8
        )
        
        # Initial convolution
        self.conv_pre = weight_norm(Conv1d(h['num_mels'], h['upsample_initial_channel'], 7, 1, padding=3))
        
        # Noise convolution
        self.noise_convs = nn.ModuleList()
        
        # Upsampling layers
        self.ups = nn.ModuleList()
        for i, (u, k) in enumerate(zip(h['upsample_rates'], h['upsample_kernel_sizes'])):
            c_cur = h['upsample_initial_channel'] // (2 ** (i + 1))
            self.ups.append(weight_norm(
                ConvTranspose1d(h['upsample_initial_channel'] // (2 ** i),
                               c_cur,
                               k, u, padding=(k - u) // 2)))
            
            # Noise conv for this layer
            if i + 1 < self.num_upsamples:
                stride_f0 = 1
                for j in range(i + 1, self.num_upsamples):
                    stride_f0 *= h['upsample_rates'][j]
                self.noise_convs.append(
                    Conv1d(1, c_cur, kernel_size=stride_f0 * 2, stride=stride_f0,
                           padding=stride_f0 // 2))
            else:
                self.noise_convs.append(Conv1d(1, c_cur, kernel_size=1))
        
        # Residual blocks
        self.resblocks = nn.ModuleList()
        for i in range(len(self.ups)):
            ch = h['upsample_initial_channel'] // (2 ** (i + 1))
            for j, (k, d) in enumerate(zip(h['resblock_kernel_sizes'],
                                           h['resblock_dilation_sizes'])):
                self.resblocks.append(ResBlock1(ch, k, d))
        
        # Output convolution
        self.conv_post = weight_norm(Conv1d(ch, 1, 7, 1, padding=3))
        
        self.ups.apply(init_weights)
        self.conv_post.apply(init_weights)

    def forward(self, mel, f0):
        """
        Args:
            mel: [B, num_mels, T] - mel spectrogram
            f0: [B, T] - fundamental frequency in Hz
        Returns:
            audio: [B, 1, T*hop_size] - generated audio
        """
        # Generate source signal
        har_source = self.m_source(f0, self.upp)
        
        # Initial conv
        x = self.conv_pre(mel)
        
        # Upsampling
        for i in range(self.num_upsamples):
            x = F.leaky_relu(x, LRELU_SLOPE)
            x = self.ups[i](x)
            
            # Add noise
            x_source = self.noise_convs[i](har_source)
            x = x + x_source
            
            # Residual blocks
            xs = None
            for j in range(self.num_kernels):
                if xs is None:
                    xs = self.resblocks[i * self.num_kernels + j](x)
                else:
                    xs += self.resblocks[i * self.num_kernels + j](x)
            x = xs / self.num_kernels
        
        x = F.leaky_relu(x)
        x = self.conv_post(x)
        x = torch.tanh(x)
        
        return x

    def remove_weight_norm(self):
        for l in self.ups:
            remove_weight_norm(l)
        for l in self.resblocks:
            l.remove_weight_norm()
        remove_weight_norm(self.conv_pre)
        remove_weight_norm(self.conv_post)


def load_checkpoint(filepath, device):
    """Load checkpoint."""
    checkpoint = torch.load(filepath, map_location=device, weights_only=False)
    return checkpoint


def convert_to_onnx(model_dir: str, output_path: str):
    """Convert PyTorch model to ONNX."""
    
    # Load config
    config_path = os.path.join(model_dir, 'config.json')
    with open(config_path, 'r') as f:
        h = json.load(f)
    
    print(f"Model config: {h}")
    
    # Create model
    device = torch.device('cpu')
    generator = Generator(h).to(device)
    
    # Load checkpoint
    ckpt_path = os.path.join(model_dir, 'model.ckpt')
    state_dict = load_checkpoint(ckpt_path, device)
    
    # Handle different checkpoint formats
    if 'generator' in state_dict:
        generator.load_state_dict(state_dict['generator'])
    elif 'state_dict' in state_dict:
        generator.load_state_dict(state_dict['state_dict'])
    else:
        generator.load_state_dict(state_dict)
    
    generator.eval()
    generator.remove_weight_norm()
    
    print("Model loaded successfully")
    
    # Create dummy inputs
    batch_size = 1
    num_frames = 100
    num_mels = h['num_mels']
    
    mel = torch.randn(batch_size, num_mels, num_frames)
    f0 = torch.abs(torch.randn(batch_size, num_frames)) * 200 + 100  # Random F0 values
    
    print(f"Dummy mel shape: {mel.shape}")
    print(f"Dummy f0 shape: {f0.shape}")
    
    # Test forward pass
    with torch.no_grad():
        output = generator(mel, f0)
        print(f"Output shape: {output.shape}")
    
    # Export to ONNX
    print(f"Exporting to ONNX: {output_path}")
    
    torch.onnx.export(
        generator,
        (mel, f0),
        output_path,
        export_params=True,
        opset_version=14,
        do_constant_folding=True,
        input_names=['mel', 'f0'],
        output_names=['audio'],
        dynamic_axes={
            'mel': {0: 'batch_size', 2: 'num_frames'},
            'f0': {0: 'batch_size', 1: 'num_frames'},
            'audio': {0: 'batch_size', 2: 'num_samples'}
        }
    )
    
    print("ONNX export completed!")
    
    # Verify ONNX model
    import onnx
    onnx_model = onnx.load(output_path)
    onnx.checker.check_model(onnx_model)
    print("ONNX model verified!")
    
    # Test with ONNX Runtime
    import onnxruntime as ort
    
    ort_session = ort.InferenceSession(output_path)
    ort_inputs = {
        'mel': mel.numpy(),
        'f0': f0.numpy()
    }
    ort_outputs = ort_session.run(None, ort_inputs)
    print(f"ONNX Runtime output shape: {ort_outputs[0].shape}")
    
    # Compare outputs
    np_output = output.numpy()
    onnx_output = ort_outputs[0]
    max_diff = np.abs(np_output - onnx_output).max()
    print(f"Max difference between PyTorch and ONNX: {max_diff}")
    
    return output_path


if __name__ == '__main__':
    model_dir = r'D:\Projects\Packed-PCNSFHifigan\pc_nsf_hifigan_model\pc_nsf_hifigan_44.1k_hop512_128bin_2025.02'
    output_path = r'D:\Projects\Packed-PCNSFHifigan\pitch_editor_juce\models\pc_nsf_hifigan.onnx'
    
    # Create output directory
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    convert_to_onnx(model_dir, output_path)
