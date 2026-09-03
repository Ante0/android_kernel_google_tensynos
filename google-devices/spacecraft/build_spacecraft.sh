#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

tools/build_dist.sh spacecraft "$@"

last_exit=$?

if [[ ( $last_exit != 0 && $BUILD_TARGET =~ "build-and-test" ) || ( $last_exit != 0 && $BUILD_TARGET =~ "sim-gem5" ) ]]; then
  echo "build_and_test: skip tests due to build failed."
  exit 1
elif [[ ( $last_exit == 0 && $BUILD_TARGET =~ "build-and-test" ) || ( $last_exit == 0 && $BUILD_TARGET =~ "sim-gem5" ) ]]; then
  echo "build_and_test: start the tests..."
  SCRIPTDIR=$PWD/$(dirname "$0")
  export PIXEL_KERNEL=${PWD}
  echo "BL_REPO_BRANCH = [$BL_REPO_BRANCH]"
  echo "GPAR_BRANCH = [$GPAR_BRANCH]"
  echo "GPAR_TARGET = [$GPAR_TARGET]"
  echo "AG_BRANCH = [$AG_BRANCH]"
  echo "AG_TARGET = [$AG_TARGET]"
  echo "PWD = [$PWD]"
  echo "SCRIPTDIR = [$SCRIPTDIR]"
  echo "DIST_DIR = [$DIST_DIR]"
  echo "SOC = [$SOC]"
  ${SCRIPTDIR}/../../../../tools/gem5test/test.sh $BL_REPO_BRANCH $GPAR_BRANCH $GPAR_TARGET \
  $AG_BRANCH $AG_TARGET $DIST_DIR $SOC $BUILD_TARGET
else
	exit $last_exit
fi
