#pragma once
// User-level commands shared by menus, toolbar and keyboard shortcuts.

namespace cf::app {

class AppContext;

namespace cmd {
void newFile(AppContext& ctx);
void open(AppContext& ctx);
void save(AppContext& ctx);
void saveAs(AppContext& ctx);
void importStep(AppContext& ctx);
void exportStep(AppContext& ctx);
void exportStl(AppContext& ctx);
void loadDemo(AppContext& ctx);
} // namespace cmd

} // namespace cf::app
