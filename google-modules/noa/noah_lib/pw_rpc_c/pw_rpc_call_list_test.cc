#include "pw_rpc_c/pw_rpc_call_list.h"
#include "gtest/gtest.h"
#include "pw_rpc_c/pw_status.h"

namespace noa::module::linux_rpc_client::pw_rpc_c {
namespace {

constexpr uint32_t kTestCallNum = 5;

void PrepareCalls(PwRpcCallList* list) {
  static PwRpcCall call[kTestCallNum];
  for (uint32_t i = 0; i < kTestCallNum; i++) {
    call[i].call_id = i;
    ASSERT_EQ(PwRpcCallListPut(list, &call[i]), kPwStatusOk);
  }
  ASSERT_EQ(PwRpcCallListSize(list), kTestCallNum);
}

TEST(PwRpcCallTest, GetCall) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  for (uint32_t i = 0; i < kTestCallNum; i++) {
    PwRpcCall* call = PwRpcCallListGet(&list, i);
    EXPECT_NE(call, nullptr);
  }
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, GetWithInvalidCallId) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  PwRpcCall* call = PwRpcCallListGet(&list, kTestCallNum + 1);
  EXPECT_EQ(call, nullptr);
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, RemoveWithInvalidCallId) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  EXPECT_EQ(PwRpcCallListRemove(&list, kTestCallNum), kPwStatusNotFound);
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, RemoveCallFromHead) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  PwRpcCall* call = nullptr;
  EXPECT_EQ(PwRpcCallListRemove(&list, 0), kPwStatusOk);
  call = PwRpcCallListGet(&list, 0);
  EXPECT_EQ(call, nullptr);
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, RemoveCallAtMiddle) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  PwRpcCall* call = nullptr;
  EXPECT_EQ(PwRpcCallListRemove(&list, 1), kPwStatusOk);
  call = PwRpcCallListGet(&list, 1);
  EXPECT_EQ(call, nullptr);
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, RemoveCallFromTail) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  PwRpcCall* call = nullptr;
  EXPECT_EQ(PwRpcCallListRemove(&list, kTestCallNum - 1), kPwStatusOk);
  call = PwRpcCallListGet(&list, kTestCallNum - 1);
  EXPECT_EQ(call, nullptr);
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, RemoveAllFromHead) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  PwRpcCall* call = nullptr;
  for (uint32_t i = 0; i < kTestCallNum; i++) {
    call = PwRpcCallListGet(&list, i);
    EXPECT_NE(call, nullptr);
    EXPECT_EQ(PwRpcCallListRemove(&list, i), kPwStatusOk);
    call = PwRpcCallListGet(&list, i);
    EXPECT_EQ(call, nullptr);
  }
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, RemoveAllFromTail) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  PwRpcCall* call = nullptr;
  for (int32_t i = kTestCallNum - 1; i >= 0; i--) {
    call = PwRpcCallListGet(&list, i);
    EXPECT_NE(call, nullptr);
    EXPECT_EQ(PwRpcCallListRemove(&list, i), kPwStatusOk);
    call = PwRpcCallListGet(&list, i);
    EXPECT_EQ(call, nullptr);
  }
  PwRpcCallListDeinit(&list);
}

TEST(PwRpcCallTest, RemoveFromMiddle) {
  PwRpcCallList list;
  PwRpcCallListInit(&list);
  PrepareCalls(&list);
  constexpr uint32_t kRemovingCallId = 3;
  PwRpcCall* call = nullptr;
  for (uint32_t i = 0; i < kTestCallNum; i++) {
    call = PwRpcCallListGet(&list, i);
    EXPECT_NE(call, nullptr);
    if (i == kRemovingCallId) {
      EXPECT_EQ(PwRpcCallListRemove(&list, i), kPwStatusOk);
      call = PwRpcCallListGet(&list, i);
      EXPECT_EQ(call, nullptr);
    }
  }
  PwRpcCallListDeinit(&list);
}
}  // namespace
}  // namespace noa::module::linux_rpc_client::pw_rpc_c
