// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "filtered_c.h"
#include "resize_adapter.hpp"
#include <memory>
// Zero-copy publication bridge; shared Prepared must be immutable thereafter.
ExboxFilteredPacket* exbox_filtered_packet_share(const ExboxFilteredKey&,std::shared_ptr<const resize_adapter::Prepared>);
