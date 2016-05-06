#!/bin/bash
cd ${PBS_O_WORKDIR}
TOOLS=../../build/tools

export HDF5_DISABLE_VERSION_CHECK=1
export PYTHONPATH=.

#GLOG_logtostderr=1  $TOOLS/caffe train -solver lstm_solver_RGB.prototxt -weights single_frame_all_layers_hyb_RGB_iter_5000.caffemodel -gpu 1  2>&1 | tee log.txt
GLOG_logtostderr=1  $TOOLS/caffe train -solver lstm_solver_RGB.prototxt -snapshot snapshots_lstm_RGB_iter_7000.solverstate -gpu 1 2>&1 | tee log.txt
echo "Done."
