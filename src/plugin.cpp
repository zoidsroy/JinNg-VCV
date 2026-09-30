#include "plugin.hpp"
#include "Serialize.hpp"

Plugin* pluginInstance;
iqs::RefTables gRefTables;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelIndexedQuadSeq);
}

// Plugin-wide settings, stored by Rack in settings.json: the user reference tables.
json_t* settingsToJson() {
	json_t* rootJ = json_object();
	json_t* tablesJ = json_array();
	for (const iqs::VoltageTable& t : gRefTables.user)
		json_array_append_new(tablesJ, iqs::tableToJson(t));
	json_object_set_new(rootJ, "userTables", tablesJ);
	return rootJ;
}

void settingsFromJson(json_t* rootJ) {
	json_t* tablesJ = json_object_get(rootJ, "userTables");
	for (int i = 0; i < iqs::NUM_USER_TABLES; i++)
		iqs::tableFromJson(gRefTables.user[i], json_array_get(tablesJ, i));
}