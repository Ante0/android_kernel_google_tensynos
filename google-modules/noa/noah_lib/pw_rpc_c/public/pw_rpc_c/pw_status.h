#ifndef PW_STATUS_H
#define PW_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif
/* Copy from pw_status/status.h */
typedef enum {
  kPwStatusOk = 0,                  // Use OkStatus() in C++
  kPwStatusCancelled = 1,           // Use Status::Cancelled() in C++
  kPwStatusUnknown = 2,             // Use Status::Unknown() in C++
  kPwStatusInvalidArgument = 3,     // Use Status::InvalidArgument() in C++
  kPwStatusDeadlineExceeded = 4,    // Use Status::DeadlineExceeded() in C++
  kPwStatusNotFound = 5,            // Use Status::NotFound() in C++
  kPwStatusAlreadyExists = 6,       // Use Status::AlreadyExists() in C++
  kPwStatusPermissionDenied = 7,    // Use Status::PermissionDenied() in C++
  kPwStatusResourceExhausted = 8,   // Use Status::ResourceExhausted() in C++
  kPwStatusFailedPrecondition = 9,  // Use Status::FailedPrecondition() in C++
  kPwStatusAborted = 10,            // Use Status::Aborted() in C++
  kPwStatusOutOfRange = 11,         // Use Status::OutOfRange() in C++
  kPwStatusUnimplemented = 12,      // Use Status::Unimplemented() in C++
  kPwStatusInternal = 13,           // Use Status::Internal() in C++
  kPwStatusUnavailable = 14,        // Use Status::Unavailable() in C++
  kPwStatusDataLoss = 15,           // Use Status::DataLoss() in C++
  kPwStatusUnauthenticated = 16,    // Use Status::Unauthenticated() in C++
} PwStatus;                         // Use pw::Status in C++
#ifdef __cplusplus
}
#endif
#endif /* PW_STATUS_H */
