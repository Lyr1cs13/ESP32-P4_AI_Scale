import argparse
import os
from pathlib import Path
from typing import Iterable, List, Sequence

import numpy as np
import torch
from PIL import Image
from torch.utils.data import DataLoader, Dataset

from esp_ppq import QuantizationSettingFactory
from esp_ppq.core import QuantizationPolicy, QuantizationProperty
from esp_ppq.api import espdl_quantize_onnx
from esp_ppq.parser.espdl import layout_patterns as espdl_layout_patterns


DEFAULT_ONNX_PATH = r"c:\Dpan\Quantization\best160.onnx"
IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
_PER_TENSOR_POWER_OF_2_POLICY = QuantizationPolicy(
    QuantizationProperty.SYMMETRICAL
    + QuantizationProperty.LINEAR
    + QuantizationProperty.PER_TENSOR
    + QuantizationProperty.POWER_OF_2
)


def patch_espdl_concat_negative_axis() -> None:
    original_export = espdl_layout_patterns.ResetConcatPattern.export

    if getattr(original_export, "_001camera_negative_axis_patched", False):
        return

    def patched_export(self, op, graph, **kwargs):
        if op.type == "Concat" and "axis" in op.attributes:
            axis = int(op.attributes["axis"])
            if axis < 0 and op.inputs:
                rank = len(op.inputs[0].shape)
                if rank > 0:
                    op.attributes["axis"] = axis + rank
        return original_export(self, op, graph, **kwargs)

    patched_export._001camera_negative_axis_patched = True
    espdl_layout_patterns.ResetConcatPattern.export = patched_export


def patch_espdl_quantizer_per_tensor_params() -> None:
    from esp_ppq.quantization.quantizer.EspdlQuantizer import BaseEspdlQuantizer

    original_create = BaseEspdlQuantizer.create_espdl_quant_config

    if getattr(original_create, "_001camera_per_tensor_params_patched", False):
        return

    def patched_create(self, operation, num_of_bits, quant_min, quant_max, bias_bits):
        config = original_create(self, operation, num_of_bits, quant_min, quant_max, bias_bits)

        if operation.type in {"Conv", "ConvTranspose", "Gemm"} and operation.num_of_input > 1:
            weight_config = config.input_quantization_config[1]
            if weight_config.policy.has_property(QuantizationProperty.PER_CHANNEL):
                weight_config.channel_axis = None
                weight_config.policy = _PER_TENSOR_POWER_OF_2_POLICY

        if operation.type in {"Conv", "ConvTranspose", "Gemm"} and operation.num_of_input > 2:
            bias_config = config.input_quantization_config[-1]
            if bias_config.policy.has_property(QuantizationProperty.PER_CHANNEL):
                bias_config.channel_axis = None
                bias_config.policy = _PER_TENSOR_POWER_OF_2_POLICY

        return config

    patched_create._001camera_per_tensor_params_patched = True
    BaseEspdlQuantizer.create_espdl_quant_config = patched_create


def parse_shape(shape_text: str) -> List[int]:
    parts = [item.strip() for item in shape_text.split(",") if item.strip()]
    if len(parts) < 4:
        raise argparse.ArgumentTypeError("input shape must look like 1,3,160,160")
    try:
        shape = [int(item) for item in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("input shape must contain integers only") from exc
    if shape[0] != 1:
        raise argparse.ArgumentTypeError("batch size must be 1 for espdl quantization")
    return shape


def parse_csv_floats(text: str) -> List[float]:
    return [float(item.strip()) for item in text.split(",") if item.strip()]


class CalibrationImageDataset(Dataset):
    def __init__(
        self,
        image_dir: str,
        input_shape: Sequence[int],
        mean: Sequence[float],
        std: Sequence[float],
        rgb: bool = True,
    ) -> None:
        self.image_dir = Path(image_dir)
        self.channels = input_shape[1]
        self.height = input_shape[2]
        self.width = input_shape[3]
        self.mean = np.asarray(mean, dtype=np.float32).reshape(-1, 1, 1)
        self.std = np.asarray(std, dtype=np.float32).reshape(-1, 1, 1)
        self.rgb = rgb

        self.files = sorted(
            [
                file
                for file in self.image_dir.rglob("*")
                if file.is_file() and file.suffix.lower() in IMAGE_EXTENSIONS
            ]
        )
        if not self.files:
            raise FileNotFoundError(f"no calibration images found in: {self.image_dir}")

        if self.channels not in (1, 3):
            raise ValueError(f"only 1 or 3 channels are supported, got {self.channels}")
        if len(mean) != self.channels or len(std) != self.channels:
            raise ValueError("mean/std length must match model input channels")

    def __len__(self) -> int:
        return len(self.files)

    def __getitem__(self, index: int) -> torch.Tensor:
        image = Image.open(self.files[index])
        if self.channels == 3:
            image = image.convert("RGB")
        else:
            image = image.convert("L")
        image = image.resize((self.width, self.height), Image.BILINEAR)

        array = np.asarray(image, dtype=np.float32)
        if self.channels == 1:
            array = np.expand_dims(array, axis=0)
        else:
            if not self.rgb:
                array = array[..., ::-1]
            array = np.transpose(array, (2, 0, 1))
        array = (array / 255.0 - self.mean) / self.std
        return torch.from_numpy(array)


class RandomCalibrationDataset(Dataset):
    def __init__(self, input_shape: Sequence[int], sample_count: int) -> None:
        self.shape = list(input_shape[1:])
        self.sample_count = sample_count

    def __len__(self) -> int:
        return self.sample_count

    def __getitem__(self, index: int) -> torch.Tensor:
        return torch.rand(self.shape, dtype=torch.float32)


def collate_fn(batch):
    if isinstance(batch, torch.Tensor):
        return batch.to("cpu")
    if isinstance(batch, list) and batch and isinstance(batch[0], torch.Tensor):
        return torch.stack(batch, dim=0).to("cpu")
    raise TypeError(f"unsupported batch type for collate_fn: {type(batch)!r}")


def build_dataloader(args: argparse.Namespace) -> DataLoader:
    if args.calib_dir:
        dataset = CalibrationImageDataset(
            image_dir=args.calib_dir,
            input_shape=args.input_shape,
            mean=args.mean,
            std=args.std,
            rgb=not args.bgr,
        )
        print(f"using {len(dataset)} calibration images from: {args.calib_dir}")
    else:
        dataset = RandomCalibrationDataset(
            input_shape=args.input_shape,
            sample_count=max(args.calib_steps, args.random_samples),
        )
        print("warning: no calibration image directory provided, using random tensors instead")

    return DataLoader(
        dataset=dataset,
        batch_size=1,
        shuffle=False,
        num_workers=0,
    )


def infer_default_output_path(onnx_path: str) -> str:
    onnx_file = Path(onnx_path)
    return str(onnx_file.with_suffix(".espdl"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Quantize an ONNX model to ESP-DL format.")
    parser.add_argument("--onnx", default=DEFAULT_ONNX_PATH, help="path to the ONNX model")
    parser.add_argument(
        "--output",
        default=None,
        help="output .espdl path, defaults to the same name as the ONNX file",
    )
    parser.add_argument(
        "--input-shape",
        type=parse_shape,
        default=[1, 3, 160, 160],
        help="model input shape, e.g. 1,3,160,160",
    )
    parser.add_argument(
        "--calib-dir",
        default=None,
        help="directory containing representative calibration images",
    )
    parser.add_argument(
        "--target",
        default="esp32p4",
        choices=["esp32p4", "esp32s3", "c"],
        help="quantization target",
    )
    parser.add_argument(
        "--bits",
        type=int,
        default=8,
        choices=[8, 16],
        help="quantization bit width",
    )
    parser.add_argument(
        "--calib-steps",
        type=int,
        default=32,
        help="number of calibration steps",
    )
    parser.add_argument(
        "--random-samples",
        type=int,
        default=32,
        help="number of random samples when calib-dir is not provided",
    )
    parser.add_argument(
        "--mean",
        type=parse_csv_floats,
        default=[0.0, 0.0, 0.0],
        help="normalization mean, e.g. 0,0,0 or 0.485,0.456,0.406",
    )
    parser.add_argument(
        "--std",
        type=parse_csv_floats,
        default=[1.0, 1.0, 1.0],
        help="normalization std, e.g. 1,1,1 or 0.229,0.224,0.225",
    )
    parser.add_argument(
        "--bgr",
        action="store_true",
        help="treat calibration images as BGR order before normalization",
    )
    parser.add_argument(
        "--skip-export",
        action="store_true",
        help="run quantization analysis without exporting .espdl",
    )
    parser.add_argument(
        "--export-test-values",
        action="store_true",
        help="export test values alongside the espdl model",
    )
    parser.add_argument(
        "--verbose",
        type=int,
        default=1,
        help="esp_ppq verbose level",
    )
    args = parser.parse_args()

    output_path = args.output or infer_default_output_path(args.onnx)
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)

    dataloader = build_dataloader(args)
    setting = QuantizationSettingFactory.espdl_setting()
    patch_espdl_concat_negative_axis()
    patch_espdl_quantizer_per_tensor_params()
    sample_batch = next(iter(dataloader))
    collated_sample = collate_fn(sample_batch)

    print(f"onnx model   : {args.onnx}")
    print(f"output model : {output_path}")
    print(f"input shape  : {args.input_shape}")
    print(f"target       : {args.target}")
    print(f"bits         : {args.bits}")
    print(f"calib steps  : {args.calib_steps}")
    print(f"sample batch : {tuple(sample_batch.shape)}")
    print(f"model input  : {tuple(collated_sample.shape)}")

    espdl_quantize_onnx(
        onnx_import_file=args.onnx,
        espdl_export_file=output_path,
        calib_dataloader=dataloader,
        calib_steps=args.calib_steps,
        input_shape=args.input_shape,
        inputs=None,
        target=args.target,
        num_of_bits=args.bits,
        collate_fn=collate_fn,
        dispatching_override=None,
        setting=setting,
        device="cuda" if torch.cuda.is_available() else "cpu",
        error_report=True,
        skip_export=args.skip_export,
        export_test_values=args.export_test_values,
        verbose=args.verbose,
    )

    print("quantization finished")


if __name__ == "__main__":
    main()
