#pragma once
namespace rex { class Runtime; namespace system { class KernelState; } }
namespace av2 {
void initialize_title_environment(rex::Runtime*,rex::system::KernelState*);
bool prepare_title_system_pages(rex::Runtime&);
}