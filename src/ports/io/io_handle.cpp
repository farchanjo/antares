#include "ports/io/io_handle.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace antares::ports::io {
namespace {

IoError fromErrno(int code) noexcept
{
    switch (code) {
    case EMFILE:
    case ENFILE:
        return IoError::tooManyDescriptors;
    default:
        return IoError::system;
    }
}

} // namespace

IoHandle::~IoHandle()
{
    if (descriptor_ >= 0) {
        ::close(descriptor_);
    }
}

IoHandle::IoHandle(IoHandle &&other) noexcept
    : descriptor_(other.release())
    , kind_(other.kind_)
    , size_(other.size_)
{
}

IoHandle &IoHandle::operator=(IoHandle &&other) noexcept
{
    if (this != &other) {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
        descriptor_ = other.release();
        kind_ = other.kind_;
        size_ = other.size_;
    }
    return *this;
}

std::expected<IoHandle, IoError> IoHandle::adopt(int descriptor) noexcept
{
    if (descriptor < 0) {
        return std::unexpected(IoError::system);
    }

    struct stat info {};
    if (::fstat(descriptor, &info) != 0) {
        return std::unexpected(fromErrno(errno));
    }

    IoHandle handle;
    handle.descriptor_ = descriptor;

    if (S_ISREG(info.st_mode)) {
        handle.kind_ = HandleKind::file;
        handle.size_ = static_cast<std::uint64_t>(info.st_size);
    } else if (S_ISSOCK(info.st_mode) || S_ISFIFO(info.st_mode) || S_ISCHR(info.st_mode)) {
        handle.kind_ = HandleKind::stream;
    } else {
        handle.kind_ = HandleKind::other;
    }

    return handle;
}

std::expected<IoHandle, IoError> IoHandle::openFile(const char *path, Access access) noexcept
{
    int flags = O_CLOEXEC;
    switch (access) {
    case Access::read:
        flags |= O_RDONLY;
        break;
    case Access::write:
        flags |= O_WRONLY;
        break;
    case Access::readWrite:
        flags |= O_RDWR;
        break;
    }

    const int descriptor = ::open(path, flags);
    if (descriptor < 0) {
        return std::unexpected(fromErrno(errno));
    }

    auto adopted = adopt(descriptor);
    if (!adopted) {
        ::close(descriptor);
    }
    return adopted;
}

int IoHandle::native() const noexcept { return descriptor_; }

HandleKind IoHandle::kind() const noexcept { return kind_; }

bool IoHandle::valid() const noexcept { return descriptor_ >= 0; }

bool IoHandle::isFile() const noexcept { return kind_ == HandleKind::file; }

std::uint64_t IoHandle::size() const noexcept { return size_; }

int IoHandle::release() noexcept
{
    const int descriptor = descriptor_;
    descriptor_ = -1;
    return descriptor;
}

} // namespace antares::ports::io
