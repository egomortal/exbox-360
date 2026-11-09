// SPDX-License-Identifier: GPL-3.0-or-later
#include "search_state.hpp"
namespace {exbox::editor::SearchState state;}
// Keep the existing callers' ABI while replacing the entire state object.
namespace ae_search {
void SetSearchOverlay(bool open,const char* query,const char* const* rows,int count){state.overlay(open,query,rows,count);}
int GetSearchStatusLine(char* output,int capacity){return state.status(output,capacity);}
void SetSearchApplied(bool value){state.applied(value);}
void RequestCatalogRebuild(){state.rebuild();}
bool ConsumeCatalogRebuildRequest(){return state.consume_rebuild();}
void SetSearchLabelPossessed(bool value){state.possessed(value);}
}
