// Makes the ring fail to open, so the factory must fall back to epoll.
//
// Preloaded over the conformance binary. `io_uring_queue_init` is the exported
// entry the engine calls; returning ENOSYS is what a kernel with
// io_uring_disabled, or a seccomp profile that blocks the syscall, looks like
// from userspace. Production links the real liburing.

#include <cerrno>
#include <liburing.h>

extern "C" __attribute__((visibility("default"))) int io_uring_queue_init(unsigned entries, struct io_uring *ring, unsigned flags)
{
    (void)entries;
    (void)ring;
    (void)flags;
    return -ENOSYS;
}
