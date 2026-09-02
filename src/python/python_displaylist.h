#pragma once

class F2DDrawer;
struct _object;

namespace PythonDisplayList
{
	// True while at least one item is registered; cheap early-out for the
	// per-frame HUD path.
	bool HasItems();

	// Re-renders every registered item. Called each HUD frame from
	// DBaseStatusBar::DrawTopStuff, right after RenderOverlay. hudState is
	// the EHudState the status bar is drawing for.
	void Render(F2DDrawer* drawer, int hudState);

	// Drops all actor-anchored items. Called on map unload; screen-space
	// items intentionally survive map transitions.
	void PurgeWorldItems();

	// Adds the bd.draw_* functions to the biaseddoom module. Returns false
	// when the functions could not be registered.
	bool AddFunctions(_object* module);
}
