#pragma once
namespace adrena {
// Register all in-tree (built-in) plugins with PluginManager::Instance().
// Must be called exactly once, before any upscale path requests a plugin.
void RegisterBuiltinPlugins();
} // namespace adrena
