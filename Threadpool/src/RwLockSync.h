#pragma once

#include <shared_mutex>

using RwLock            = std::shared_mutex;
using ReadSyncGuard     = std::shared_lock<RwLock>;
using WriteSyncGuard    = std::unique_lock<RwLock>;