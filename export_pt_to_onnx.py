import argparse
import shutil
from pathlib import Path


DEFAULT_PT_PATH = r"c:\Dpan\Quantization\best(160).pt"
DEFAULT_OUTPUT_PATH = r"c:\Dpan\Quantization\best160.onnx"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export a YOLO .pt model to ONNX for ESP-DL quantization.")
    parser.add_argument("--pt", default=DEFAULT_PT_PATH, help="path to the input .pt model")
    parser.add_argument("--output", default=DEFAULT_OUTPUT_PATH, help="path to the output .onnx model")
    parser.add_argument("--imgsz", type=int, default=160, help="export image size, must match quantization input shape")
    parser.add_argument("--opset", type=int, default=13, help="ONNX opset version")
    parser.add_argument("--batch", type=int, default=1, help="static batch size")
    parser.add_argument("--dynamic", action="store_true", help="export dynamic input shape")
    parser.add_argument("--simplify", action="store_true", help="simplify ONNX during export")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    pt_path = Path(args.pt)
    output_path = Path(args.output)

    if not pt_path.is_file():
        raise FileNotFoundError(f"input model not found: {pt_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        from ultralytics import YOLO
    except ImportError as exc:
        raise SystemExit("missing dependency: install ultralytics first, e.g. `pip install ultralytics`") from exc

    model = YOLO(str(pt_path))
    exported = model.export(
        format="onnx",
        imgsz=args.imgsz,
        batch=args.batch,
        dynamic=args.dynamic,
        simplify=args.simplify,
        opset=args.opset,
        nms=False,
    )

    exported_path = Path(exported)
    if exported_path.resolve() != output_path.resolve():
        shutil.copyfile(exported_path, output_path)
        exported_path.unlink()

    print(f"exported ONNX: {output_path}")
    print(f"quantize with: python quantize_onnx_espdl.py --onnx \"{output_path}\" --input-shape 1,3,{args.imgsz},{args.imgsz}")


if __name__ == "__main__":
    main()
