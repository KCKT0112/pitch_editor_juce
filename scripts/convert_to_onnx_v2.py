#!/usr/bin/env python3
"""
Convert PC-NSF-HiFiGAN PyTorch model to ONNX format for C++ inference.

This script handles the mini_nsf=true variant with single source_conv
instead of noise_convs array.
"""

import os
import sys
import json
import torch
import torch.nn as nn
import torch.nn.functional as F
import numpy as np
from pathlib import Path
from torch.nn import Conv1d, ConvTranspose1d
from torch.nn.utils import weight_norm, remove_weight_norm


# Configuration
MODEL_DIR = Path(r"D:\Projects\Packed-PCNSFHifigan\pc_nsf_hifigan_model\pc_nsf_hifigan_44.1k_hop512_128bin_2025.02")
OUTPUT_DIR = Path(r"D:\Projects\Packed-PCNSFHifigan\pitch_editor_juce\models")


LRELU_SLOPE = 0.1


def init_weights(m, mean=0.0, std=0.01):
    classname = m.__class__.__name__
    if classname.find("Conv") != -1:
        m.weight.data.normal_(mean, std)


def get_padding(kernel_size, dilation=1):
    return int((kernel_size * dilation - dilation) / 2)


class AttrDict(dict):
    def __init__(self, *args, **kwargs):
        super(AttrDict, self).__init__(*args, **kwargs)
        self.__dict__ = self


class ResBlock1(nn.Module):
    def __init__(self, h, channels, kernel_size=3, dilation=(1, 3, 5)):
        super().__init__()
        self.h = h
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


class Generator(nn.Module):
    """
    PC-NSF-HiFiGAN Generator with mini_nsf=true mode.
    
    In mini_nsf mode:
    - Uses fastsinegen instead of SourceModuleHnNSF
    - Uses single source_conv at upsample layer 1 instead of noise_convs array
    - source_sr = sampling_rate / prod(upsample_rates[2:])
    - upp = prod(upsample_rates[:2])
    """
    def __init__(self, h):
        super().__init__()
        self.h = h
        self.num_kernels = len(h.resblock_kernel_sizes)
        self.num_upsamples = len(h.upsample_rates)
        self.mini_nsf = h.mini_nsf
        self.noise_sigma = getattr(h, 'noise_sigma', None)
        
        # mini_nsf mode configuration
        if h.mini_nsf:
            self.source_sr = h.sampling_rate / int(np.prod(h.upsample_rates[2:]))
            self.upp = int(np.prod(h.upsample_rates[:2]))
        else:
            raise NotImplementedError("This script only supports mini_nsf=true mode")
        
        # Pre-conv
        self.conv_pre = weight_norm(Conv1d(h.num_mels, h.upsample_initial_channel, 7, 1, padding=3))
        
        # Upsampling layers and residual blocks
        self.ups = nn.ModuleList()
        self.resblocks = nn.ModuleList()
        
        ch = h.upsample_initial_channel
        for i, (u, k) in enumerate(zip(h.upsample_rates, h.upsample_kernel_sizes)):
            ch //= 2
            self.ups.append(weight_norm(ConvTranspose1d(2 * ch, ch, k, u, padding=(k - u) // 2)))
            
            # In mini_nsf mode, add source_conv at layer 1
            if h.mini_nsf and i == 1:
                self.source_conv = Conv1d(1, ch, 1)
                self.source_conv.apply(init_weights)
            
            # Residual blocks
            for k2, d in zip(h.resblock_kernel_sizes, h.resblock_dilation_sizes):
                self.resblocks.append(ResBlock1(h, ch, k2, d))
        
        # Post-conv
        self.conv_post = weight_norm(Conv1d(ch, 1, 7, 1, padding=3))
        self.ups.apply(init_weights)
        self.conv_post.apply(init_weights)

    def fastsinegen(self, f0):
        """Generate sine wave source signal for mini_nsf mode."""
        n = torch.arange(1, self.upp + 1, device=f0.device)
        s0 = f0.unsqueeze(-1) / self.source_sr
        ds0 = F.pad(s0[:, 1:, :] - s0[:, :-1, :], (0, 0, 0, 1))
        rad = s0 * n + 0.5 * ds0 * n * (n - 1) / self.upp
        rad2 = torch.fmod(rad[..., -1:].float() + 0.5, 1.0) - 0.5
        rad_acc = rad2.cumsum(dim=1).fmod(1.0).to(f0)
        rad += F.pad(rad_acc[:, :-1, :], (0, 0, 1, 0))
        rad = rad.reshape(f0.shape[0], 1, -1)
        sines = torch.sin(2 * np.pi * rad)
        return sines

    def forward(self, mel, f0):
        """
        Forward pass.
        
        Args:
            mel: Mel spectrogram [batch, num_mels, frames]
            f0: Fundamental frequency [batch, frames]
            
        Returns:
            audio: Generated waveform [batch, 1, samples]
        """
        # Generate source signal (mini_nsf mode)
        har_source = self.fastsinegen(f0)
        
        # Pre-conv
        x = self.conv_pre(mel)
        if self.noise_sigma is not None and self.noise_sigma > 0:
            x += self.noise_sigma * torch.randn_like(x)
        
        # Upsampling
        for i in range(self.num_upsamples):
            x = F.leaky_relu(x, LRELU_SLOPE)
            x = self.ups[i](x)
            
            # Add source at layer 1 (mini_nsf mode)
            if self.mini_nsf and i == 1:
                x_source = self.source_conv(har_source)
                x = x + x_source
            
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
        """Remove weight normalization for inference."""
        for l in self.ups:
            remove_weight_norm(l)
        for l in self.resblocks:
            l.remove_weight_norm()
        remove_weight_norm(self.conv_pre)
        remove_weight_norm(self.conv_post)


def convert_to_onnx():
    """Convert the PyTorch model to ONNX format."""
    
    # Load config
    config_path = MODEL_DIR / "config.json"
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    h = AttrDict(config)
    print(f"Model config:")
    print(f"  sampling_rate={h.sampling_rate}")
    print(f"  hop_size={h.hop_size}")
    print(f"  num_mels={h.num_mels}")
    print(f"  mini_nsf={h.mini_nsf}")
    print(f"  upsample_rates={h.upsample_rates}")
    
    # Create model
    model = Generator(h)
    
    # Load checkpoint
    ckpt_path = MODEL_DIR / "model.ckpt"
    print(f"\nLoading checkpoint from {ckpt_path}...")
    ckpt = torch.load(ckpt_path, map_location='cpu', weights_only=False)
    
    if 'generator' in ckpt:
        model.load_state_dict(ckpt['generator'])
    else:
        model.load_state_dict(ckpt)
    
    print("Checkpoint loaded successfully!")
    
    model.eval()
    
    # Remove weight norm for inference
    model.remove_weight_norm()
    print("Weight normalization removed.")
    
    # Create dummy inputs
    batch_size = 1
    mel_frames = 100  # 100 frames
    mel = torch.randn(batch_size, h.num_mels, mel_frames)
    f0 = torch.abs(torch.randn(batch_size, mel_frames)) * 200 + 200  # F0 around 200-400 Hz
    
    # Test forward pass
    print("\nTesting forward pass...")
    with torch.no_grad():
        output = model(mel, f0)
    print(f"Forward pass successful! Output shape: {output.shape}")
    
    # Export to ONNX
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    onnx_path = OUTPUT_DIR / "pc_nsf_hifigan.onnx"
    
    print(f"\nExporting to {onnx_path}...")
    
    torch.onnx.export(
        model,
        (mel, f0),
        onnx_path,
        input_names=['mel', 'f0'],
        output_names=['audio'],
        dynamic_axes={
            'mel': {0: 'batch', 2: 'frames'},
            'f0': {0: 'batch', 1: 'frames'},
            'audio': {0: 'batch', 2: 'samples'}
        },
        opset_version=14,
        do_constant_folding=True
    )
    
    print(f"ONNX model saved to {onnx_path}")
    
    # Verify the model
    import onnx
    onnx_model = onnx.load(onnx_path)
    onnx.checker.check_model(onnx_model)
    print("ONNX model verification passed!")
    
    # Get file size
    file_size_mb = os.path.getsize(onnx_path) / (1024 * 1024)
    print(f"ONNX model size: {file_size_mb:.2f} MB")
    
    # Test with ONNX Runtime
    try:
        import onnxruntime as ort
        print("\nTesting with ONNX Runtime...")
        session = ort.InferenceSession(str(onnx_path))
        
        # Run inference
        outputs = session.run(
            None,
            {'mel': mel.numpy(), 'f0': f0.numpy()}
        )
        
        print(f"ONNX Runtime inference successful!")
        print(f"Output shape: {outputs[0].shape}")
        
        # Compare outputs
        torch_out = output.numpy()
        onnx_out = outputs[0]
        max_diff = np.abs(torch_out - onnx_out).max()
        print(f"Max difference between PyTorch and ONNX outputs: {max_diff:.6f}")
        
    except ImportError:
        print("onnxruntime not installed, skipping runtime test")


if __name__ == "__main__":
    convert_to_onnx()
