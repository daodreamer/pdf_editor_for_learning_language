"""Derive the shipped Supertonic 3 graphs from the pinned upstream fp32 ONNX files.

Run only with the pinned interpreter from tools/requirements-speech.lock.txt (bootstrap-speech.py does this).
- vector_estimator: ONNX Runtime dynamic quantization, per-channel uint8 weights for MatMul/Gemm/Conv.
  uint8 because the bundled ONNX Runtime 1.23.2 CPU provider has no ConvInteger kernel for int8 weights.
- duration_predictor, text_encoder, vocoder: weight-only fp16 storage. Every large float initializer
  becomes fp16 plus Cast(to=FLOAT); ONNX Runtime folds the Cast at session creation, so arithmetic stays fp32.
Full int8 (including the vocoder) was rejected: Whisper read-back WER ~100% in the 2026-11-11 evaluation.
"""
import argparse
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
from onnxruntime.quantization import QuantType, quantize_dynamic


def fp16_weights(source, target):
    model = onnx.load(str(source))
    graph = model.graph
    initializers, casts = [], []
    for item in graph.initializer:
        if item.data_type == TensorProto.FLOAT and int(np.prod(item.dims)) >= 1024:
            half = numpy_helper.from_array(numpy_helper.to_array(item).astype(np.float16), item.name + '__fp16')
            initializers.append(half)
            casts.append(helper.make_node('Cast', [half.name], [item.name], to=TensorProto.FLOAT, name=item.name + '__cast'))
        else:
            initializers.append(item)
    del graph.initializer[:]
    graph.initializer.extend(initializers)
    nodes = list(graph.node)
    del graph.node[:]
    graph.node.extend(casts + nodes)
    onnx.checker.check_model(model)
    onnx.save(model, str(target))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('duration_predictor', 'text_encoder', 'vocoder'):
        fp16_weights(args.source / f'{name}.onnx', args.output / f'{name}.onnx')
    quantize_dynamic(str(args.source / 'vector_estimator.onnx'), str(args.output / 'vector_estimator.onnx'),
                     weight_type=QuantType.QUInt8, op_types_to_quantize=['MatMul', 'Gemm', 'Conv'], per_channel=True)


if __name__ == '__main__':
    main()
