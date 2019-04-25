#!/usr/bin/env python3
###################################################################################################
#
#  Project:  DevKitKeywordSpotter
#  File:     update.py
#  Authors:  Chris Lovett
#
#  Requires: Python 3.x
#
###################################################################################################
import argparse
import os
import sys
import zipfile
from shutil import copyfile


ELL_ROOT = os.getenv("ELL_ROOT")
if not ELL_ROOT:
    print("Please set your ELL_ROOT environment variable pointing to the location of your ELL git repo")
sys.path += [os.path.join(ELL_ROOT, "tools", "utilities", "pythonlibs")]
sys.path += [os.path.join(ELL_ROOT, "tools", "utilities", "pythonlibs", "audio", "training")]
sys.path += [os.path.join(ELL_ROOT, "tools", "importers", "onnx")]


def unzip(path, name):
    with zipfile.ZipFile(path) as zf:
        for member in zf.infolist():
            if member.filename == name:
                zf.extract(member)
                return name
    raise Exception("zip archive {} does not contain expected member {}".format(path, name))


def onnx_import(path):
    import onnx_import
    filename = onnx_import.convert(path, os.getcwd())
    if os.path.basename(filename) != "classifier.ell":
        copyfile(filename, "classifier.ell")
    return "classifier.ell"


def get_metadata(featurizer):
    import find_ell
    import ell
    map = ell.model.Map(featurizer)
    input_size = map.GetInputShape().Size()
    sample_rate = None
    iter = map.GetModel().GetNodes()
    while iter.IsValid():
        node = iter.Get()
        s = node.GetMetadataValue("sample_rate")
        if s:
            sample_rate = int(s)
        iter.Next()
    return (input_size, sample_rate)


def add_vad(filename, input_size, sample_rate):
    import model_editor
    editor = model_editor.ModelEditor(filename)

    changed = False
    # these numbers work well on the MXCHIP board
    tau_up = 1
    tau_down = 0.1
    large_input = 4
    gain_att = 0.1
    threshold_up = 1
    threshold_down = 1
    level_threshold = 0.1
    node_type = None
    for node in editor.find_rnns():
        node_type = node.GetRuntimeTypeName()
        changed |= editor.add_vad(node, sample_rate, input_size, tau_up, tau_down,
                                  large_input, gain_att, threshold_up, threshold_down,
                                  level_threshold)

    if changed:
        print("VoiceActivityDetector node added to {}".format(node_type))
    elif editor.vad_node is None:
        print("model does not contain any RNN, GRU or LSTM nodes?")
    return editor


def add_sink(editor, node_type, function_name):
    if editor.attach_sink(node_type, function_name):
        print("sink named {} updated to node {}".format(function_name, node_type))


def to_hard_sigmoid(filename):
    with open(filename, "r") as f:
        lines = f.readlines()

    for i in range(len(lines)):
        line = lines[i]
        if "Sigmoid" in line and "HardSigmoid" not in line:
            lines[i] = line.replace("Sigmoid", "HardSigmoid")

    with open(filename, "w") as f:
        f.writelines(lines)


def write_model_properties(sample_rate, featurizer_input_size, featurizer_output_size, classifier_output_size):
    with open("model_properties.h", "w") as f:
        f.write("const int SAMPLE_RATE = {};\n".format(sample_rate))
        f.write("const int FEATURIZER_INPUT_SIZE = {};\n".format(featurizer_input_size))
        f.write("const int FEATURIZER_OUTPUT_SIZE = {};\n".format(featurizer_output_size))
        f.write("const int CLASSIFIER_OUTPUT_SIZE = {};\n".format(classifier_output_size))


def create_categories_header(classifier_output_size):
    with open("categories.txt", "r") as f:
        lines = [x.strip() for x in f.readlines()]

    if len(lines) != classifier_output_size:
        raise Exception("### found {} categories, but expecting {}".format(len(lines), classifier_output_size))

    with open("categories.h", "w") as f:
        f.write("// {} keywords\n".format(len(lines)))
        f.write("static const char* const categories[] = {\n")
        for line in lines:
            f.write("    \"{}\",\n".format(line))
        f.write("};")


def update(path):
    if not os.path.isdir(path):
        print("Path must be an existing directory")
        return

    files = os.listdir(path)
    if "featurizer.ell" in files:
        copyfile(os.path.join(path, "featurizer.ell"), "featurizer.ell")
    elif "featurizer.ell.zip" in files:
        unzip(os.path.join(path, "featurizer.ell.zip"), "featurizer.ell")
    else:
        print("No featurizer.ell or featurizer.ell.zip found in source path")
    featurizer = "featurizer.ell"

    onnx_file = [x for x in files if x.endswith(".onnx") or x.endswith(".onnx.zip")]
    if len(onnx_file) == 0:
        print("No onnx file found in source path")
        return

    onnx_file = os.path.join(path, onnx_file[0])
    local_onnx_file = os.path.basename(onnx_file)
    if onnx_file.endswith(".onnx.zip"):
        local_onnx_file = unzip(onnx_file, os.path.splitext(local_onnx_file)[0])
    else:
        copyfile(onnx_file, local_onnx_file)

    ell_file = onnx_import(local_onnx_file)

    input_size, sample_rate = get_metadata(featurizer)
    editor = add_vad(ell_file, input_size, sample_rate)
    add_sink(editor, "VoiceActivityDetector", "VadCallback")
    editor.save(ell_file)

    to_hard_sigmoid(ell_file)

    classifier_input_size = editor.map.GetInputShape().Size()
    classifier_output_size = editor.map.GetOutputShape().Size()
    write_model_properties(sample_rate, input_size, classifier_input_size, classifier_output_size)

    categories = os.path.join(path, "categories.txt")
    if not os.path.isfile(categories):
        print("Missing categories.txt")
    else:
        copyfile(categories, "categories.txt")

    create_categories_header(classifier_output_size)


if __name__ == "__main__":
    arg_parser = argparse.ArgumentParser(description="Copy new featurizer and classifier from a given path and "
                                                     "prepare classifier for use on mxchip")

    # options
    arg_parser.add_argument("path", help="folder to copy new featurizer and classifier from")

    args = arg_parser.parse_args()
    update(args.path)
