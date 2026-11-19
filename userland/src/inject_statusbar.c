/*
 * libhakios.statusbar.dylib: the status bar's clock moved clear of a camera hole in the top-left corner of the panel.
 *
 * Darwin userland 16.5 lays the status bar out as on the phone it believes it is (iPhone10,3, an iPhone X: the "split"
 * layout, the time centred in the left ear). The realme GT Neo 3T (spartan) has its front camera in a hole at the top left,
 * where that time is drawn. The status bar is drawn by every process that shows one -- SpringBoard and each application
 * (build.sh's INJECT: "SpringBoard @apps", the kernel's kern.exec_inject group for every program in an application bundle)
 * -- so this library is in all of them.
 *
 * What it does: after the status bar (_UIStatusBar) has laid itself out, the view that shows the time
 * (_UIStatusBarStringView whose text is a time, in the left part of the bar) is measured in the screen's points and, if its
 * left edge is over the hole, moved right by a translation just as large as needed: the time then starts right after the
 * hole. Nothing of the bar's own layout is changed, and in a process with no UIKit the library does nothing.
 *
 * The hole (Android's own description of it, dumpsys display: cutoutSpec "M -407 68.5 A 34 34 0 0 1 -475 68.5 ...", from
 * the middle of a 1080-pixel-wide panel): a circle of radius 34 centred 99 pixels from the left edge, so its right edge is
 * at 133 pixels; HOLE_CLEAR_PX leaves a margin after it. Points are pixels times the screen's width in points over 1080.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PANEL_WIDTH_PX          1080.0
#define HOLE_CLEAR_PX           (133.0 + 14.0)  /* the hole's right edge, and a margin */

static void (*original_layout)(id self, SEL sel);
static Class string_view_class;

static id
msg(id object, const char *selector)
{
	return ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName(selector));
}

/* the screen's width in points (UIScreen: not to be asked before UIKit is up, so asked here, at layout time) */
static double
screen_points(void)
{
	static double width;

	if (width == 0) {
		Class screen_class = objc_getClass("UIScreen");
		id screen = screen_class ? ((id (*)(Class, SEL))objc_msgSend)(screen_class, sel_registerName("mainScreen")) : nil;
		CGRect bounds = screen ? ((CGRect (*)(id, SEL))objc_msgSend)(screen, sel_registerName("bounds")) : CGRectZero;

		width = bounds.size.width < bounds.size.height ? bounds.size.width : bounds.size.height;   /* (portrait width) */
	}
	return width;
}

/* "12:45", "9:41 AM": a digit first and a colon in it */
static bool
looks_like_time(id view)
{
	id text = msg(view, "text");
	const char *s = text ? ((const char *(*)(id, SEL))objc_msgSend)(text, sel_registerName("UTF8String")) : NULL;

	return s != NULL && isdigit((unsigned char)s[0]) && strchr(s, ':') != NULL && strlen(s) < 16;
}

/* once to the console (an application's standard error goes nowhere): what was measured and done */
static void
say_once(const char *text)
{
	static int said;
	int fd;

	if (said++ >= 12 || (fd = open("/dev/console", O_WRONLY)) < 0) {
		return;
	}
	(void)write(fd, text, strlen(text));
	close(fd);
}

/* the status bar's string view that shows the time, at most 16 levels down; nil if none */
static id
find_time(id view, unsigned depth)
{
	if (view == nil || depth > 16) {
		return nil;
	}
	if (string_view_class != Nil && ((BOOL (*)(id, SEL, Class))objc_msgSend)(view, sel_registerName("isKindOfClass:"),
	    string_view_class) && looks_like_time(view)) {
		return view;
	}
	{
		id subviews = msg(view, "subviews");
		unsigned long count = subviews ? ((unsigned long (*)(id, SEL))objc_msgSend)(subviews, sel_registerName("count")) : 0;

		for (unsigned long i = 0; i < count; i++) {
			id found = find_time(((id (*)(id, SEL, unsigned long))objc_msgSend)(subviews, sel_registerName("objectAtIndex:"), i),
			    depth + 1);

			if (found != nil) {
				return found;
			}
		}
	}
	return nil;
}

/*
 * The time, measured in the bar's own points; then not the time's view but the bar's subview that holds it -- its region,
 * which clips what is outside it (19 November: the time moved inside it was cut off and not seen at all) -- is moved right
 * by what the time's left edge needs to clear the hole. The measuring is done with that region where the bar put it.
 */
static void
move_time(id bar)
{
	SEL set_transform = sel_registerName("setTransform:"), superview = sel_registerName("superview");
	id time = find_time(bar, 0), region;
	double points = screen_points(), clear, need;
	CGRect bounds, frame;
	char line[200];

	if (time == nil || points <= 0) {
		return;
	}
	/*
	 * Up from the time to the widest view that is still the left part alone (under 60% of the screen): the bar's direct
	 * subview is its foreground, every item of both sides (19 November: moved, it pushed the battery off the right edge).
	 */
	for (region = time; region != nil; ) {
		id up = ((id (*)(id, SEL))objc_msgSend)(region, superview);
		CGRect up_bounds;

		if (up == bar || up == nil) {
			break;
		}
		up_bounds = ((CGRect (*)(id, SEL))objc_msgSend)(up, sel_registerName("bounds"));
		if (up_bounds.size.width >= 0.6 * points) {
			break;
		}
		region = up;
	}
	if (region == nil) {
		return;
	}
	((void (*)(id, SEL, CGAffineTransform))objc_msgSend)(region, set_transform, CGAffineTransformIdentity);
	bounds = ((CGRect (*)(id, SEL))objc_msgSend)(time, sel_registerName("bounds"));
	frame = ((CGRect (*)(id, SEL, CGRect, id))objc_msgSend)(time, sel_registerName("convertRect:toView:"), bounds, bar);
	clear = HOLE_CLEAR_PX * points / PANEL_WIDTH_PX;
	need = clear - frame.origin.x;
	snprintf(line, sizeof(line), "statusbar: the time at x %.1f (%.1f wide) of a %.0f-point screen; clear of the hole from %.1f: "
	    "its region (%s) moved %.1f\n", frame.origin.x, frame.size.width, points, clear, object_getClassName(region),
	    need > 0 && frame.origin.x < points / 2 ? need : 0.0);
	say_once(line);
	if (need > 0 && frame.origin.x < points / 2 && region == time) {
		/*
		 * The time is a direct subview of the bar's foreground, nothing clips it (seen on the phone: clipsToBounds 0, no
		 * mask), and yet moved by a transform it was not seen (19 November): the bar animates its items through their
		 * transforms. So its frame is moved instead -- to the place where it clears the hole, not by an amount, so that a
		 * second pass over a layout that was not redone leaves it there.
		 */
		CGRect own = ((CGRect (*)(id, SEL))objc_msgSend)(time, sel_registerName("frame"));

		own.origin.x += need;
		((void (*)(id, SEL, CGRect))objc_msgSend)(time, sel_registerName("setFrame:"), own);
	} else if (need > 0 && frame.origin.x < points / 2) {
		((void (*)(id, SEL, CGAffineTransform))objc_msgSend)(region, set_transform, CGAffineTransformMakeTranslation(need, 0));
	}
}

static void
status_bar_layout(id self, SEL sel)
{
	if (original_layout != NULL) {
		original_layout(self, sel);
	}
	move_time(self);
}

__attribute__((constructor))
static void
statusbar_start(void)
{
	Class bar = objc_getClass("_UIStatusBar");
	SEL layout = sel_registerName("layoutSubviews");
	Method method;

	string_view_class = objc_getClass("_UIStatusBarStringView");
	if (bar == Nil || string_view_class == Nil || (method = class_getInstanceMethod(bar, layout)) == NULL) {
		return;                 /* (no UIKit in this process, or not the one this was written for) */
	}
	/* _UIStatusBar's own -layoutSubviews if it has one; if it only inherits UIView's, one is added to it alone */
	original_layout = (void (*)(id, SEL))method_getImplementation(method);
	if (!class_addMethod(bar, layout, (IMP)status_bar_layout, method_getTypeEncoding(method))) {
		original_layout = (void (*)(id, SEL))method_setImplementation(method, (IMP)status_bar_layout);
	}
}
