# SPDX-License-Identifier: GPL-2.0-only

orphan_test() {
  # Launch an orphan grandchild process
  sleep 100 &
  CHILD_PID=$!
  echo "${CHILD_PID}" > "${OUT}/orphan.pid"
  echo "Started orphan ${CHILD_PID}"
  sleep 100
}
