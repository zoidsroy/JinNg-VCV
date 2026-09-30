#pragma once
#include <rack.hpp>
#include "core/VoltageTables.hpp"

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelIndexedQuadSeq;

// The user reference tables, shared by every instance and saved in Rack's settings.
extern iqs::RefTables gRefTables;