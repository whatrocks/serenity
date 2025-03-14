/*
 * Copyright (c) 2019-2020, Sergey Bugaev <bugaevc@serenityos.org>
 * Copyright (c) 2022, Liav A. <liavalb@hotmail.co.il>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <Kernel/FileSystem/FileSystem.h>
#include <Kernel/FileSystem/Inode.h>
#include <Kernel/Forward.h>

namespace Kernel {

class TmpFS final : public FileSystem {
    friend class TmpFSInode;

public:
    virtual ~TmpFS() override;
    static ErrorOr<NonnullLockRefPtr<FileSystem>> try_create();
    virtual ErrorOr<void> initialize() override;

    virtual StringView class_name() const override { return "TmpFS"sv; }

    virtual bool supports_watchers() const override { return true; }

    virtual Inode& root_inode() override;

private:
    TmpFS();

    LockRefPtr<TmpFSInode> m_root_inode;

    unsigned m_next_inode_index { 1 };
    unsigned next_inode_index();
};

}
