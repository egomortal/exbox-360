// SPDX-License-Identifier: GPL-3.0-or-later
#include "../modules/editor_layer/user_access.hpp"
#include <cassert>
#include <initializer_list>
using namespace exbox::editor;
int main() {
    for (uint32_t user = 0; user < 512; ++user) {
        const auto p = privilege(user);
        if (user == 0 || user == 255) { assert(p.status == AccessStatus::success && p.write && p.first == 1); }
        else { assert(!p.write); assert(p.status == (user < 4 ? AccessStatus::no_such_user : AccessStatus::invalid_argument)); }
        for (bool rating : {false, true}) {
            const auto q = restriction(user, rating);
            assert(q.write == (user == 0));
            assert(q.status == (user ? AccessStatus::no_such_user : AccessStatus::success));
            assert(q.first == (rating ? 63U : 0U) && q.second == 0);
        }
        assert(local_user(user) == (user >= 4 ? AccessStatus::invalid_argument : user ? AccessStatus::no_such_user : AccessStatus::success));
    }
    assert(privilege(UINT32_MAX).status == AccessStatus::invalid_argument);
    assert(restriction(UINT32_MAX, true).status == AccessStatus::no_such_user);
    assert(access_check().write && access_check().first == 1);
    assert(online_compatibility() == 1);
}
