"""Writes the synthetic YOLOv8 and RF-DETR models detector_model_test loads.

Each has the interface of an Ultralytics export (an `images` input of 1x3x640x640 and an `output0`
of 1x(4+classes[+32])x8400; the segmentation model adds `output1`, 1x32x160x160 prototype masks), but
instead of a network it emits a few hand-placed candidate boxes, so the test knows exactly which
detections should come out. Scores are in the 640x640 letterboxed input's pixel space, as YOLOv8's
are. The models are a few KB, so they're checked in; rerun this after changing the tables below:

    python3 -m venv /tmp/venv && /tmp/venv/bin/pip install onnx
    /tmp/venv/bin/python perception/trt_detector/object_detector_lib/test/testdata/make_test_models.py

The RF-DETR models have the interface of an rfdetr export instead: an `input` of 1x3xRxR, `dets`
(1xQx4, normalized cx, cy, w, h), `labels` (1xQxC class logits) and, for segmentation, `masks`
(1xQx(R/4)x(R/4) mask logits).

The candidates are mirrored as constants in detector_model_test.cpp; keep the two in step.
"""

import math
import pathlib

import onnx
from onnx import TensorProto, helper

OUT_DIR = pathlib.Path(__file__).parent
ANCHORS = 8400

# Detection model: 3 classes. (anchor, cx, cy, w, h, class, score).
DETECT_CLASSES = 3
DETECT_BOXES = [
    (0, 320, 320, 100, 50, 1, 0.9),  # kept
    (1, 324, 320, 100, 50, 1, 0.8),  # IoU 0.92 with anchor 0, same class: suppressed by NMS
    (2, 100, 100, 40, 40, 2, 0.7),  # kept
    (3, 500, 100, 40, 40, 0, 0.3),  # below any sensible confidence threshold
    (5, 320, 320, 100, 50, 2, 0.6),  # same box as anchor 0 but another class: NMS is per class
]
# Anchor 4 scores class 0 with the mean of the input tensor, so it only appears for a bright image:
# proof the image actually reaches the engine.
BRIGHTNESS_BOX = (4, 500, 500, 60, 60, 0)

# Segmentation model: 2 classes, one candidate covering the whole letterboxed input, whose mask
# (prototype 0, the only one it weights) is the top half of the input.
SEGMENT_CLASSES = 2
SEGMENT_BOX = (0, 320, 320, 640, 640, 0, 0.9)
MASK_PROTOS = 32
PROTO_SIZE = 160

# RF-DETR models. RF_RES is smaller than any real checkpoint's, which keeps engine builds quick; nothing
# in the detector depends on it.
RF_RES = 128
RF_QUERIES = 10
RF_MASK = RF_RES // 4
RF_OFF_LOGIT = -10.0  # Every (query, class) not listed below scores sigmoid(-10), ~0.

# Detection model: 4 classes. (query, cx, cy, w, h, [(class, score), ...]), in the image's normalized
# coordinates: the model's input is the image stretched, not letterboxed.
RF_DETECT_CLASSES = 4
RF_DETECT_QUERIES = [
    (0, 0.5, 0.5, 0.25, 0.125, [(1, 0.9)]),  # kept
    (1, 0.515625, 0.5, 0.25, 0.125, [(1, 0.8)]),  # overlaps query 0, same class: kept, there's no NMS
    (2, 0.15625, 0.15625, 0.0625, 0.0625, [(2, 0.7)]),  # kept
    (3, 0.78125, 0.15625, 0.0625, 0.0625, [(3, 0.3)]),  # below any sensible confidence threshold
    (5, 0.5, 0.75, 0.125, 0.125, [(0, 0.65), (2, 0.6)]),  # one query, two classes: two detections
]
# Query 4 scores class 0 with the logit of the mean of the input's first channel, so it only appears
# when the image is red after RGB conversion and ImageNet normalization.
RF_RED_QUERY = (4, 0.78125, 0.78125, 0.09375, 0.09375, 0)

# Segmentation model: 2 classes. Query 0 covers the image with class 0 and masks its top half; query 1
# covers it with class 1 and masks its left half. The rest score nothing.
RF_SEGMENT_CLASSES = 2
RF_SEGMENT_QUERIES = [
    (0, 0.5, 0.5, 1.0, 1.0, [(0, 0.9)]),
    (1, 0.5, 0.5, 1.0, 1.0, [(1, 0.8)]),
]


def const(name, dims, values, dtype=TensorProto.FLOAT):
    return helper.make_node(
        "Constant", [], [name], value=helper.make_tensor(name + "_value", dtype, dims, values)
    )


def box_updates(boxes, classes):
    """ScatterND indices/updates writing each box's coordinates and score into output0."""
    indices, updates = [], []
    for anchor, cx, cy, w, h, cls, score in boxes:
        for row, value in enumerate((cx, cy, w, h)):
            indices.append((0, row, anchor))
            updates.append(float(value))
        indices.append((0, 4 + cls, anchor))
        updates.append(float(score))
    return indices, updates


def input_mean_nodes():
    """`mean` (shape [1]): the input tensor's mean, so every graph depends on its input."""
    return [
        helper.make_node("ReduceMean", ["images"], ["mean_scalar"], keepdims=0),
        const("one_shape", [1], [1], TensorProto.INT64),
        helper.make_node("Reshape", ["mean_scalar", "one_shape"], ["mean"]),
    ]


def scatter_nodes(prefix, data, out, indices, updates):
    return [
        const(prefix + "_indices", [len(indices), 3], [v for i in indices for v in i], TensorProto.INT64),
        const(prefix + "_updates", [len(updates)], updates),
        helper.make_node("ScatterND", [data, prefix + "_indices", prefix + "_updates"], [out]),
    ]


def zeros_node(name, dims, value=0.0):
    return helper.make_node(
        "ConstantOfShape",
        [name + "_shape"],
        [name],
        value=helper.make_tensor(name + "_fill", TensorProto.FLOAT, [1], [value]),
    ), const(name + "_shape", [len(dims)], dims, TensorProto.INT64)


def save(graph, name):
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)], producer_name=__file__)
    model.ir_version = 8
    onnx.checker.check_model(model, full_check=True)
    onnx.save(model, OUT_DIR / name)


images = helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, 640, 640])


def detect_model():
    rows = 4 + DETECT_CLASSES
    zeros, zeros_shape = zeros_node("zeros", [1, rows, ANCHORS])
    indices, updates = box_updates(DETECT_BOXES, DETECT_CLASSES)
    anchor, cx, cy, w, h, cls = BRIGHTNESS_BOX
    b_indices, b_updates = box_updates([(anchor, cx, cy, w, h, cls, 0.0)], DETECT_CLASSES)
    nodes = [zeros_shape, zeros, *input_mean_nodes()]
    nodes += scatter_nodes("boxes", "zeros", "with_boxes", indices, updates)
    # The brightness box's coordinates, then its score from the input.
    nodes += scatter_nodes("bright_box", "with_boxes", "with_bright_box", b_indices[:-1], b_updates[:-1])
    nodes += [
        const("bright_score_index", [1, 3], list(b_indices[-1]), TensorProto.INT64),
        helper.make_node("ScatterND", ["with_bright_box", "bright_score_index", "mean"], ["output0"]),
    ]
    output0 = helper.make_tensor_value_info("output0", TensorProto.FLOAT, [1, rows, ANCHORS])
    save(helper.make_graph(nodes, "yolov8_detect_test", [images], [output0]), "detect_3_classes.onnx")


def segment_model():
    rows = 4 + SEGMENT_CLASSES + MASK_PROTOS
    zeros, zeros_shape = zeros_node("zeros", [1, rows, ANCHORS])
    indices, updates = box_updates([SEGMENT_BOX], SEGMENT_CLASSES)
    # The box weights prototype 0 only.
    indices.append((0, 4 + SEGMENT_CLASSES, SEGMENT_BOX[0]))
    updates.append(1.0)
    nodes = [zeros_shape, zeros, *input_mean_nodes()]
    nodes += scatter_nodes("boxes", "zeros", "with_boxes", indices, updates)
    nodes += [
        const("zero", [1], [0.0]),
        helper.make_node("Mul", ["mean", "zero"], ["no_op"]),
        helper.make_node("Add", ["with_boxes", "no_op"], ["output0"]),
    ]

    # Prototype 0: strongly on over the top half of the grid, strongly off below. The rest are zero.
    half = PROTO_SIZE // 2
    top, top_shape = zeros_node("proto_top", [1, 1, half, PROTO_SIZE], 10.0)
    bottom, bottom_shape = zeros_node("proto_bottom", [1, 1, half, PROTO_SIZE], -10.0)
    rest, rest_shape = zeros_node("proto_rest", [1, MASK_PROTOS - 1, PROTO_SIZE, PROTO_SIZE])
    nodes += [top_shape, top, bottom_shape, bottom, rest_shape, rest]
    nodes += [
        helper.make_node("Concat", ["proto_top", "proto_bottom"], ["proto_0"], axis=2),
        helper.make_node("Concat", ["proto_0", "proto_rest"], ["protos"], axis=1),
        helper.make_node("Add", ["protos", "no_op"], ["output1"]),
    ]

    output0 = helper.make_tensor_value_info("output0", TensorProto.FLOAT, [1, rows, ANCHORS])
    output1 = helper.make_tensor_value_info(
        "output1", TensorProto.FLOAT, [1, MASK_PROTOS, PROTO_SIZE, PROTO_SIZE]
    )
    save(helper.make_graph(nodes, "yolov8_segment_test", [images], [output0, output1]), "segment_2_classes.onnx")


def logit(p):
    return math.log(p / (1 - p))


def rf_input():
    return helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, RF_RES, RF_RES])


def rf_red_mean_nodes():
    """`red` (shape [1]): the mean of the input's first channel, so every graph depends on its input."""
    return [
        const("red_starts", [4], [0, 0, 0, 0], TensorProto.INT64),
        const("red_ends", [4], [1, 1, RF_RES, RF_RES], TensorProto.INT64),
        helper.make_node("Slice", ["input", "red_starts", "red_ends"], ["red_channel"]),
        helper.make_node("ReduceMean", ["red_channel"], ["red_scalar"], keepdims=0),
        const("red_shape", [1], [1], TensorProto.INT64),
        helper.make_node("Reshape", ["red_scalar", "red_shape"], ["red"]),
    ]


def rf_boxes_and_labels(queries, classes, extra_query=None):
    """Nodes writing `dets` and `labels_scattered` for `queries`; `extra_query` gets a box but no score."""
    dets, labels = [[0.0] * 4 for _ in range(RF_QUERIES)], [[RF_OFF_LOGIT] * classes for _ in range(RF_QUERIES)]
    for query, cx, cy, w, h, scores in queries:
        dets[query] = [cx, cy, w, h]
        for cls, score in scores:
            labels[query][cls] = logit(score)
    if extra_query is not None:
        query, cx, cy, w, h = extra_query
        dets[query] = [cx, cy, w, h]
    return [
        const("dets_value", [1, RF_QUERIES, 4], [v for box in dets for v in box]),
        const("labels_value", [1, RF_QUERIES, classes], [v for row in labels for v in row]),
    ]


def rf_detect_model():
    nodes = rf_red_mean_nodes()
    query, cx, cy, w, h, cls = RF_RED_QUERY
    nodes += rf_boxes_and_labels(RF_DETECT_QUERIES, RF_DETECT_CLASSES, (query, cx, cy, w, h))
    nodes += [
        const("zero", [1], [0.0]),
        helper.make_node("Mul", ["red", "zero"], ["no_op"]),
        helper.make_node("Add", ["dets_value", "no_op"], ["dets"]),
        const("red_index", [1, 3], [0, query, cls], TensorProto.INT64),
        helper.make_node("ScatterND", ["labels_value", "red_index", "red"], ["labels"]),
    ]
    outputs = [
        helper.make_tensor_value_info("dets", TensorProto.FLOAT, [1, RF_QUERIES, 4]),
        helper.make_tensor_value_info("labels", TensorProto.FLOAT, [1, RF_QUERIES, RF_DETECT_CLASSES]),
    ]
    save(helper.make_graph(nodes, "rfdetr_detect_test", [rf_input()], outputs), "rfdetr_detect_4_classes.onnx")


def rf_segment_model():
    nodes = rf_red_mean_nodes()
    nodes += rf_boxes_and_labels(RF_SEGMENT_QUERIES, RF_SEGMENT_CLASSES)
    half = RF_MASK // 2
    masks = [[[0.0] * RF_MASK for _ in range(RF_MASK)] for _ in range(RF_QUERIES)]
    for y in range(RF_MASK):
        for x in range(RF_MASK):
            masks[0][y][x] = 10.0 if y < half else -10.0
            masks[1][y][x] = 10.0 if x < half else -10.0
    nodes += [
        const("masks_value", [1, RF_QUERIES, RF_MASK, RF_MASK], [v for m in masks for row in m for v in row]),
        const("zero", [1], [0.0]),
        helper.make_node("Mul", ["red", "zero"], ["no_op"]),
        helper.make_node("Add", ["dets_value", "no_op"], ["dets"]),
        helper.make_node("Add", ["labels_value", "no_op"], ["labels"]),
        helper.make_node("Add", ["masks_value", "no_op"], ["masks"]),
    ]
    outputs = [
        helper.make_tensor_value_info("dets", TensorProto.FLOAT, [1, RF_QUERIES, 4]),
        helper.make_tensor_value_info("labels", TensorProto.FLOAT, [1, RF_QUERIES, RF_SEGMENT_CLASSES]),
        helper.make_tensor_value_info("masks", TensorProto.FLOAT, [1, RF_QUERIES, RF_MASK, RF_MASK]),
    ]
    save(helper.make_graph(nodes, "rfdetr_segment_test", [rf_input()], outputs), "rfdetr_segment_2_classes.onnx")


if __name__ == "__main__":
    detect_model()
    segment_model()
    rf_detect_model()
    rf_segment_model()
