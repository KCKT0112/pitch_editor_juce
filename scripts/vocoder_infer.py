#!/usr/bin/env python3
"""
Vocoder inference script for JUCE pitch editor.
Called as a subprocess to perform AI synthesis.

Usage:
    python vocoder_infer.py --model MODEL_PATH --mel MEL_PATH --f0 F0_PATH --output OUTPUT_PATH
"""

import argparse
import json
import os
import sys
import numpy as np

# Add project root to path
script_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.dirname(os.path.dirname(script_dir))
sys.path.insert(0, os.path.join(project_root, 'pitch_editor', 'src'))

import torch
import soundfile as sf


class SineGen(torch.nn.Module):
    """Sine wave generator for NSF-HiFiGAN."""
    
    def __init__(self, samp_rate, harmonic_num=0, sine_amp=0.1,
                 noise_std=0.003, voiced_threshold=0, flag_for_pulse=False):
        super().__init__()
        self.sine_amp = sine_amp
        self.noise_std = noise_std
        self.harmonic_num = harmonic_num
        self.dim = harmonic_num + 1
        self.sampling_rate = samp_rate
        self.voiced_threshold = voiced_threshold

    def _f02uv(self, f0):
        uv = torch.ones_like(f0)
        uv = uv * (f0 > self.voiced_threshold)
        return uv

    def forward(self, f0, upp):
        with torch.no_grad():
            f0 = f0[:, None].transpose(1, 2)
            f0_buf = torch.zeros(f0.shape[0], f0.shape[1], self.dim, device=f0.device)
            f0_buf[:, :, 0] = f0[:, :, 0]
            for idx in range(self.harmonic_num):
                f0_buf[:, :, idx + 1] = f0_buf[:, :, 0] * (idx + 2)
            rad_values = (f0_buf / self.sampling_rate) % 1
            rand_ini = torch.rand(f0_buf.shape[0], f0_buf.shape[2], device=f0_buf.device)
            rand_ini[:, 0] = 0
            rad_values[:, 0, :] = rad_values[:, 0, :] + rand_ini
            tmp_over_one = torch.cumsum(rad_values, 1)
            tmp_over_one *= upp
            tmp_over_one = torch.nn.functional.interpolate(
                tmp_over_one.transpose(2, 1),
                scale_factor=float(upp),
                mode='linear',
                align_corners=True
            ).transpose(2, 1)
            rad_values = torch.nn.functional.interpolate(
                rad_values.transpose(2, 1),
                scale_factor=float(upp),
                mode='nearest'
            ).transpose(2, 1)
            tmp_over_one %= 1
            tmp_over_one_idx = (tmp_over_one[:, 1:, :] - tmp_over_one[:, :-1, :]) < 0
            cumsum_shift = torch.zeros_like(rad_values)
            cumsum_shift[:, 1:, :] = tmp_over_one_idx * -1.0
            sine_waves = torch.sin(torch.cumsum(rad_values + cumsum_shift, dim=1) * 2 * np.pi)
            sine_waves = sine_waves * self.sine_amp
            uv = self._f02uv(f0)
            uv = torch.nn.functional.interpolate(
                uv.transpose(2, 1),
                scale_factor=float(upp),
                mode='nearest'
            ).transpose(2, 1)
            noise_amp = uv * self.noise_std + (1 - uv) * self.sine_amp / 3
            noise = noise_amp * torch.randn_like(sine_waves)
            sine_waves = sine_waves * uv + noise
        return sine_waves, uv, noise


class Vocoder:
    """PC-NSF-HiFiGAN vocoder wrapper."""
    
    def __init__(self, model_path: str, config_path: str, device: str = None):
        self.device = device or ('cuda' if torch.cuda.is_available() else 'cpu')
        
        # Load config
        with open(config_path, 'r') as f:
            self.config = json.load(f)
        
        self.sample_rate = self.config.get('sampling_rate', 44100)
        self.hop_size = self.config.get('hop_size', 512)
        self.num_mels = self.config.get('num_mels', 128)
        self.pitch_controllable = self.config.get('pc_aug', False)
        
        # Load model checkpoint
        checkpoint = torch.load(model_path, map_location=self.device, weights_only=False)
        self.model_state = checkpoint.get('generator', checkpoint)
        
        # Build model
        self._build_model()
        
        print(f"Vocoder loaded: sample_rate={self.sample_rate}, hop_size={self.hop_size}, "
              f"num_mels={self.num_mels}, pitch_controllable={self.pitch_controllable}")
    
    def _build_model(self):
        """Build the HiFiGAN generator model."""
        # Model architecture parameters from config
        h = self.config
        resblock_kernel_sizes = h.get('resblock_kernel_sizes', [3, 7, 11])
        resblock_dilation_sizes = h.get('resblock_dilation_sizes', [[1, 3, 5], [1, 3, 5], [1, 3, 5]])
        upsample_rates = h.get('upsample_rates', [8, 8, 2, 2, 2])
        upsample_initial_channel = h.get('upsample_initial_channel', 512)
        upsample_kernel_sizes = h.get('upsample_kernel_sizes', [16, 16, 4, 4, 4])
        resblock = h.get('resblock', '1')
        
        # Create generator
        from core.vocoder import Generator
        self.generator = Generator(
            h,
            self.num_mels,
            resblock,
            resblock_kernel_sizes,
            resblock_dilation_sizes,
            upsample_rates,
            upsample_initial_channel,
            upsample_kernel_sizes
        ).to(self.device)
        
        self.generator.load_state_dict(self.model_state)
        self.generator.eval()
        self.generator.remove_weight_norm()
        
        # Create sine generator for F0
        self.sine_gen = SineGen(
            self.sample_rate,
            harmonic_num=8
        )
    
    @torch.no_grad()
    def infer(self, mel: np.ndarray, f0: np.ndarray) -> np.ndarray:
        """
        Run vocoder inference.
        
        Args:
            mel: Mel spectrogram, shape (num_mels, num_frames)
            f0: F0 contour, shape (num_frames,)
        
        Returns:
            waveform: Audio waveform, shape (num_samples,)
        """
        # Convert to tensors
        mel_t = torch.FloatTensor(mel).unsqueeze(0).to(self.device)  # (1, num_mels, T)
        f0_t = torch.FloatTensor(f0).unsqueeze(0).to(self.device)    # (1, T)
        
        # Generate excitation signal
        upp = self.hop_size
        sine_waves, uv, noise = self.sine_gen(f0_t, upp)
        sine_waves = sine_waves.to(self.device)
        
        # Run through generator
        if self.pitch_controllable:
            waveform = self.generator(mel_t, sine_waves)
        else:
            waveform = self.generator(mel_t)
        
        # Convert to numpy
        waveform = waveform.squeeze().cpu().numpy()
        
        return waveform


def main():
    parser = argparse.ArgumentParser(description='Vocoder inference')
    parser.add_argument('--model', required=True, help='Path to model checkpoint')
    parser.add_argument('--config', help='Path to config.json (defaults to same dir as model)')
    parser.add_argument('--mel', required=True, help='Path to mel spectrogram .npy file')
    parser.add_argument('--f0', required=True, help='Path to F0 .npy file')
    parser.add_argument('--output', required=True, help='Output WAV file path')
    parser.add_argument('--device', default=None, help='Device (cuda/cpu)')
    
    args = parser.parse_args()
    
    # Default config path
    if args.config is None:
        args.config = os.path.join(os.path.dirname(args.model), 'config.json')
    
    # Load vocoder
    print(f"Loading vocoder from {args.model}...")
    vocoder = Vocoder(args.model, args.config, args.device)
    
    # Load mel and F0
    print(f"Loading mel from {args.mel}...")
    mel = np.load(args.mel)
    
    print(f"Loading F0 from {args.f0}...")
    f0 = np.load(args.f0)
    
    # Ensure shapes match
    min_frames = min(mel.shape[1], f0.shape[0])
    mel = mel[:, :min_frames]
    f0 = f0[:min_frames]
    
    print(f"Running inference: mel shape={mel.shape}, f0 shape={f0.shape}")
    
    # Run inference
    waveform = vocoder.infer(mel, f0)
    
    print(f"Generated waveform: {len(waveform)} samples")
    
    # Save output
    sf.write(args.output, waveform, vocoder.sample_rate)
    print(f"Saved to {args.output}")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
