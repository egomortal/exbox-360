/* Shared presenter-owned Vulkan allocation accounting; no device ownership. */
#ifndef EXBOX_GPU_BUDGET_H
#define EXBOX_GPU_BUDGET_H
#include <stdint.h>
#include <stddef.h>

#define EXBOX_GPU_BUDGET_DEFAULT (UINT64_C(240) * 1024 * 1024)
typedef struct ExboxGpuBudget {
    uint64_t limit, used, peak;
    size_t live;
} ExboxGpuBudget;
typedef struct ExboxGpuReservation {
    ExboxGpuBudget *owner;
    uint64_t bytes;
} ExboxGpuReservation;
#define EXBOX_GPU_BUDGET_INIT { EXBOX_GPU_BUDGET_DEFAULT, 0, 0, 0 }
#define EXBOX_GPU_RESERVATION_INIT { NULL, 0 }

/* All calls on the existing serial presenter owner. Budget and reservations
 * are noncopyable by contract and must outlive every associated allocation.
 * Reserve VkMemoryRequirements.size BEFORE vkAllocateMemory. Roll back on
 * allocation failure; otherwise release only AFTER vkFreeMemory. Retain both
 * allocation and reservation after an unsafe submission/quarantined client.
 * Count shared memory once, at its allocation owner, not per bound buffer.
 */
static inline int exbox_gpu_budget_reserve(ExboxGpuBudget *budget,
                                          ExboxGpuReservation *reservation,
                                          uint64_t bytes) {
    if (!budget || !reservation || !bytes || reservation->owner ||
        reservation->bytes || budget->used > budget->limit ||
        bytes > budget->limit - budget->used || budget->live == SIZE_MAX)
        return 0;
    budget->used += bytes;
    if (budget->used > budget->peak) budget->peak = budget->used;
    ++budget->live;
    reservation->owner = budget;
    reservation->bytes = bytes;
    return 1;
}

static inline int exbox_gpu_budget_release(ExboxGpuBudget *budget,
                                          ExboxGpuReservation *reservation) {
    if (!budget || !reservation || reservation->owner != budget ||
        !reservation->bytes || !budget->live || reservation->bytes > budget->used)
        return 0;
    budget->used -= reservation->bytes;
    --budget->live;
    reservation->owner = NULL;
    reservation->bytes = 0;
    return 1;
}
#endif
