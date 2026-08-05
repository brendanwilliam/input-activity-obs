#include "sources/dashboard/rendering/lol_key_labels.hpp"

#include <uiohook.h>

int main()
{
	using sources::lol_dashboard_key_label;
	return lol_dashboard_key_label(VC_A) == "A" && lol_dashboard_key_label(VC_0) == "0" &&
			       lol_dashboard_key_label(VC_BACK_SLASH) == "\\" &&
			       lol_dashboard_key_label(VC_META_L) == "⌘" && lol_dashboard_key_label(VC_META_R) == "⌘" &&
			       lol_dashboard_key_label(VC_UP) == "↑" && lol_dashboard_key_label(VC_LEFT) == "←" &&
			       lol_dashboard_key_label(VC_DOWN) == "↓" && lol_dashboard_key_label(VC_RIGHT) == "→" &&
			       lol_dashboard_key_label(VC_BACKSPACE) == "⌫" &&
			       lol_dashboard_key_label(VC_CAPS_LOCK) == "⇪" &&
			       lol_dashboard_key_label(VC_FUNCTION) == "fn" &&
			       lol_dashboard_key_label(VC_KP_7) == "KP 7"
		       ? 0
		       : 1;
}
