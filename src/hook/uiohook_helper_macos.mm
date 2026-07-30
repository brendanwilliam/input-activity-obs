/*************************************************************************
 * This file is part of input-overlay
 * git.vrsal.cc/alex/input-overlay
 * Copyright 2026 univrsal <uni@vrsal.cc>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *************************************************************************/

#include "uiohook_helper.hpp"

#include "../input/input_data.hpp"

#include <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#include <cstdarg>
#include <cstdlib>
#include <obs-module.h>
#include <pthread.h>
#include <sched.h>
#include <uiohook.h>

namespace uiohook {

    uint64_t last_scroll_time = 0;
    bool state = false;

    static uint64_t focused_window_id(pid_t pid)
    {
        AXUIElementRef application = AXUIElementCreateApplication(pid);
        if (!application)
            return 0;

        CFTypeRef focused_window = nullptr;
        const AXError focused_window_result =
            AXUIElementCopyAttributeValue(application, kAXFocusedWindowAttribute, &focused_window);
        CFRelease(application);
        if (focused_window_result != kAXErrorSuccess || !focused_window)
            return 0;

        CFTypeRef window_number = nullptr;
        const AXError window_number_result = AXUIElementCopyAttributeValue(static_cast<AXUIElementRef>(focused_window),
                                                                           CFSTR("AXWindowNumber"), &window_number);
        CFRelease(focused_window);
        if (window_number_result != kAXErrorSuccess || !window_number)
            return 0;

        int64_t number {};
        const bool converted = CFGetTypeID(window_number) == CFNumberGetTypeID() &&
                               CFNumberGetValue(static_cast<CFNumberRef>(window_number), kCFNumberSInt64Type, &number);
        CFRelease(window_number);
        return converted && number > 0 ? static_cast<uint64_t>(number) : 0;
    }

    static input_data::trace_event event_context(const uiohook_event *event)
    {
        input_data::trace_event context {};
        @autoreleasepool {
            NSRunningApplication *application = NSWorkspace.sharedWorkspace.frontmostApplication;
            if (!application)
                return context;

            NSString *bundle_identifier = application.bundleIdentifier;
            if (bundle_identifier)
                context.application_id = bundle_identifier.UTF8String;

            const pid_t pid = application.processIdentifier;
            const uint64_t focused_window = focused_window_id(pid);
            CFArrayRef windows = CGWindowListCopyWindowInfo(
                kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
            if (windows) {
                for (NSDictionary *window in (__bridge NSArray *) windows) {
                    if ([window[(id) kCGWindowOwnerPID] intValue] != pid || [window[(id) kCGWindowLayer] intValue] != 0)
                        continue;
                    const uint64_t window_id = [window[(id) kCGWindowNumber] unsignedLongLongValue];
                    if (focused_window && window_id != focused_window)
                        continue;
                    context.window_id = window_id;
                    CGRect bounds {};
                    if (CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef) window[(id) kCGWindowBounds],
                                                               &bounds)) {
                        CGDirectDisplayID display {};
                        uint32_t count {};
                        if (CGGetDisplaysWithRect(bounds, 1, &display, &count) == kCGErrorSuccess && count)
                            context.focused_display_id = display;
                    }
                    break;
                }
                CFRelease(windows);
            }
            if (event->type == EVENT_MOUSE_MOVED || event->type == EVENT_MOUSE_DRAGGED) {
                CGDirectDisplayID display {};
                uint32_t count {};
                if (CGGetDisplaysWithPoint(CGPointMake(event->data.mouse.x, event->data.mouse.y), 1, &display,
                                           &count) == kCGErrorSuccess &&
                    count)
                    context.pointer_display_id = display;
            }
        }
        return context;
    }

    static void process_event(uiohook_event *event)
    {
        static input_data thread_data;
        static constexpr uint64_t refresh_ms = 16;
        static uint64_t last_time = 0;
        const uint64_t diff = event->time - last_time;
        const bool important = event->type < EVENT_MOUSE_MOVED;

        thread_data.dispatch_uiohook_event(event, event_context(event));
        if (important || (diff >= refresh_ms && local_data::data.last_event < thread_data.last_event)) {
            last_time = event->time;
            std::lock_guard<std::mutex> lock(local_data::data.m_mutex);
            local_data::data.copy(&thread_data);
        }
    }

    static pthread_t hook_thread;
    static pthread_mutex_t hook_running_mutex;
    static pthread_mutex_t hook_control_mutex;
    static pthread_cond_t hook_control_cond;

    void *hook_thread_proc(void *arg)
    {
        const int status = hook_run();
        if (status != UIOHOOK_SUCCESS)
            *(int *) arg = status;

        pthread_cond_signal(&hook_control_cond);
        pthread_mutex_unlock(&hook_control_mutex);
        return arg;
    }

    extern "C" {
        static void logger_proc(unsigned int level, void *, const char *format, va_list args)
        {
            switch (level) {
                default:
                case LOG_LEVEL_DEBUG:
                    blogva(LOG_DEBUG, format, args);
                    break;
                case LOG_LEVEL_INFO:
                    blogva(LOG_INFO, format, args);
                    break;
                case LOG_LEVEL_WARN:
                case LOG_LEVEL_ERROR:
                    blogva(LOG_WARNING, format, args);
                    break;
            }
        }
    }

    void dispatch_proc(uiohook_event *event, void *)
    {
        switch (event->type) {
            case EVENT_HOOK_ENABLED:
                pthread_mutex_lock(&hook_running_mutex);
                pthread_cond_signal(&hook_control_cond);
                pthread_mutex_unlock(&hook_control_mutex);
                break;
            case EVENT_HOOK_DISABLED:
                pthread_mutex_lock(&hook_control_mutex);
                pthread_mutex_unlock(&hook_running_mutex);
            default:
                break;
        }
        process_event(event);
    }

    static int hook_enable()
    {
        pthread_mutex_lock(&hook_control_mutex);
        int status = UIOHOOK_FAILURE;

        pthread_attr_t hook_thread_attr;
        pthread_attr_init(&hook_thread_attr);

        int policy;
        pthread_attr_getschedpolicy(&hook_thread_attr, &policy);
        const int priority = sched_get_priority_max(policy);

        auto *hook_thread_status = static_cast<int *>(malloc(sizeof(int)));
        if (hook_thread_status)
            *hook_thread_status = UIOHOOK_FAILURE;
        if (hook_thread_status &&
            pthread_create(&hook_thread, &hook_thread_attr, hook_thread_proc, hook_thread_status) == 0) {
            const sched_param param = {.sched_priority = priority};
            if (pthread_setschedparam(hook_thread, SCHED_OTHER, &param) != 0) {
                blog(LOG_WARNING, "[input-activity] Could not set uiohook thread priority.");
            }

            pthread_cond_wait(&hook_control_cond, &hook_control_mutex);
            if (pthread_mutex_trylock(&hook_running_mutex) == 0) {
                void *thread_result = nullptr;
                pthread_join(hook_thread, &thread_result);
                status = *static_cast<int *>(thread_result);
                pthread_mutex_unlock(&hook_running_mutex);
            } else {
                status = UIOHOOK_SUCCESS;
            }
            free(hook_thread_status);
        } else {
            free(hook_thread_status);
            status = -1;
        }

        pthread_attr_destroy(&hook_thread_attr);
        pthread_mutex_unlock(&hook_control_mutex);
        return status;
    }

    void start()
    {
        if (state)
            return;

        if (!AXIsProcessTrusted()) {
            blog(LOG_WARNING,
                 "[input-activity] macOS Accessibility permission is required for keyboard and mouse capture. "
                 "Enable OBS in System Settings > Privacy & Security > Accessibility, then restart OBS.");
            return;
        }

        pthread_mutex_init(&hook_running_mutex, nullptr);
        pthread_mutex_init(&hook_control_mutex, nullptr);
        pthread_cond_init(&hook_control_cond, nullptr);
        hook_set_logger_proc(&logger_proc, nullptr);
        hook_set_dispatch_proc(&dispatch_proc, nullptr);

        const int status = hook_enable();
        if (status == UIOHOOK_SUCCESS) {
            state = true;
            return;
        }

        switch (status) {
            case UIOHOOK_ERROR_AXAPI_DISABLED:
                blog(
                    LOG_ERROR,
                    "[input-activity] macOS Accessibility permission was denied. Enable OBS in System Settings > Privacy & "
                    "Security > Accessibility, then restart OBS. (%#X)",
                    status);
                break;
            case UIOHOOK_ERROR_CREATE_EVENT_PORT:
                blog(LOG_ERROR, "[input-activity] Failed to create macOS input event port. (%#X)", status);
                break;
            case UIOHOOK_ERROR_CREATE_RUN_LOOP_SOURCE:
                blog(LOG_ERROR, "[input-activity] Failed to create macOS input run loop source. (%#X)", status);
                break;
            case UIOHOOK_ERROR_GET_RUNLOOP:
                blog(LOG_ERROR, "[input-activity] Failed to acquire macOS input run loop. (%#X)", status);
                break;
            case UIOHOOK_ERROR_CREATE_OBSERVER:
                blog(LOG_ERROR, "[input-activity] Failed to create macOS input run loop observer. (%#X)", status);
                break;
            default:
                blog(LOG_ERROR, "[input-activity] Failed to start macOS input hook. (%#X)", status);
                break;
        }

        pthread_mutex_destroy(&hook_running_mutex);
        pthread_mutex_destroy(&hook_control_mutex);
        pthread_cond_destroy(&hook_control_cond);
    }

    void stop()
    {
        if (!state)
            return;

        hook_stop();
        pthread_join(hook_thread, nullptr);
        state = false;
        pthread_mutex_destroy(&hook_running_mutex);
        pthread_mutex_destroy(&hook_control_mutex);
        pthread_cond_destroy(&hook_control_cond);
    }

    std::vector<target_display> target_displays()
    {
        std::vector<target_display> result;
        CGDirectDisplayID displays[16] {};
        uint32_t count {};
        if (CGGetActiveDisplayList(16, displays, &count) != kCGErrorSuccess)
            return result;
        result.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            const CGRect bounds = CGDisplayBounds(displays[index]);
            const int width = static_cast<int>(bounds.size.width);
            const int height = static_cast<int>(bounds.size.height);
            result.push_back({displays[index],
                              "Display " + std::to_string(index + 1) + " (" + std::to_string(width) + "x" +
                                  std::to_string(height) + ")",
                              width, height});
        }
        return result;
    }

    std::vector<target_application> target_applications()
    {
        std::vector<target_application> result;
        @autoreleasepool {
            for (NSRunningApplication *application in NSWorkspace.sharedWorkspace.runningApplications) {
                NSString *bundle_identifier = application.bundleIdentifier;
                if (!bundle_identifier)
                    continue;
                NSString *name = application.localizedName ?: bundle_identifier;
                result.push_back({bundle_identifier.UTF8String, name.UTF8String});
            }
        }
        return result;
    }

    std::vector<target_window> target_windows()
    {
        std::vector<target_window> result;
        @autoreleasepool {
            CFArrayRef windows = CGWindowListCopyWindowInfo(
                kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
            if (!windows)
                return result;
            for (NSDictionary *window in (__bridge NSArray *) windows) {
                if ([window[(id) kCGWindowLayer] intValue] != 0)
                    continue;
                NSRunningApplication *application = [NSRunningApplication
                    runningApplicationWithProcessIdentifier:[window[(id) kCGWindowOwnerPID] intValue]];
                NSString *bundle_identifier = application.bundleIdentifier;
                if (!bundle_identifier)
                    continue;
                NSString *application_name = application.localizedName ?: bundle_identifier;
                NSString *window_name = window[(id) kCGWindowName];
                NSString *label = window_name.length
                                      ? [NSString stringWithFormat:@"%@ — %@", application_name, window_name]
                                      : application_name;
                result.push_back({bundle_identifier.UTF8String, [window[(id) kCGWindowNumber] unsignedLongLongValue],
                                  label.UTF8String});
            }
            CFRelease(windows);
        }
        return result;
    }

}  // namespace uiohook
