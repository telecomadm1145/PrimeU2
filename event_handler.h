// =========================================================================
//                  类型定义 (基于您提供的头文件)
// =========================================================================
#include "your_header_file.h" // 假设头文件名为此
// struct ui_event_prime_s, struct UIMultipressEvent, enum ui_event_type_e,
// enum keycode_e 等都定义在此文件中。

// =========================================================================
//              全局变量和函数重命名 (基于我们的分析)
// =========================================================================

// --- 全局变量 ---
// DAT_30cad054: 一个包含定时器状态的全局结构体
struct {
    /* ... other fields ... */
    undefined4 unknown_fields[11];
    int timer_handle;          // [0xb] 定时器句柄, -1表示无效
    unsigned int timeout_timestamp; // [0xc] 定时器到期的时间戳
} *g_TimerState = (void*)0x30cad054;

// 其他全局状态
#define g_AnotherTimerHandle (*(int*)0x30cad068)
#define g_NetworkContext (*(void**)0x30cad060)
#define g_SomeNetworkData (*(undefined4*)(*(int*)0x30cad064 + 0x74))
#define g_Flag1 (*(int*)0x30cab120)
#define g_Flag2 (*(int*)0x30cab124)
#define g_VolumeOrChannelFlag (*(int*)0x30cab060)
#define g_KeyPressCallback (*(void (***)(void*, int, ...))0x30caff5c)

// --- 函数 ---
#define GetEvent(event_ptr) GetEvent(event_ptr) // 系统API，无需重命名
#define GetCurrentTimeTicks() thunk_FUN_3011f3d4() // 获取当前系统时间/滴答数
#define ProcessTouchEvent(x, y, event, time) FUN_302f3a70(x, y, event, time)
#define GetItemStatus(id) FUN_30105ecc(id) // 获取某个项目/按键的状态
#define ReleaseItem(id) FUN_30105ef4(id)   // 释放某个项目/按键
#define CancelAndCleanupTimer() FUN_3013a5cc()
#define SetTimer(handle_ptr, duration_ms, callback, arg) FUN_3012fd84(handle_ptr, duration_ms, callback, arg)
#define ReleaseTimerHandle(handle) FUN_3012c800(handle)
#define NotifyContext(context) FUN_30133350(context)
#define SendNetworkCommand(context, ...) FUN_3012d564(context, __VA_ARGS__)
#define ResetIdleTimerOrRefreshUI() FUN_3032a334()
#define HandleVolumeOrChannelDown() FUN_3031ed2c()

// --- 未知/内部事件代码 (不在 ui_event_type_e 中) ---
#define EV_SYSTEM_TIMER   0xf        // 系统定时器事件
#define EV_KEY_PRESS      0x100      // 高层键盘按下事件
#define EV_SYSTEM_NOTIFY  0x4000     // 系统通知/重绘事件
#define EV_TIMER_AND_KEYUP 0x100010  // 定时器和按键弹起复合事件

/**
 * @brief 主事件循环处理函数
 * @details
 *  这个函数是应用程序的核心，它在一个无限循环中等待并处理来自系统的各种事件，
 *  包括定时器、用户输入（按键、触摸）和系统通知。
 *
 * @param param_1 (unused) 未使用的参数
 * @param param_2 (unused) 未使用的参数
 * @param param_3 (unused) 未使用的参数，在函数内被用作临时指针
 */
void MainEventLoop(undefined4 param_1, undefined4 param_2, undefined4* param_3)
{
    struct ui_event_prime_s event;
    unsigned int currentTime;

    // 主事件循环
    do {
        // 1. 等待并获取下一个事件
        GetEvent(&event);
        currentTime = GetCurrentTimeTicks();

        // 2. 根据主事件代码分发事件
        // 注意：这里使用的是系统级事件代码，而非 ui_event_type_e 枚举
        switch (event.event_type) {

            // --- CASE: 系统定时器或复合事件 ---
        case EV_SYSTEM_TIMER:    // 0xf
        case EV_TIMER_AND_KEYUP: // 0x100010
        {
            // 检查全局定时器是否已过期
            // 反编译器生成的 `x + 1 < 0 == SCARRY4(x, 1)` 是 `x != -1` 的复杂写法
            if (g_TimerState->timer_handle != -1) {
                // 如果当前时间超过了设定的超时时间戳
                if (currentTime > g_TimerState->timeout_timestamp) {
                    // 定时器已超时，重置定时器状态
                    g_TimerState->timeout_timestamp = 0;

                    // 遍历事件中的所有子项目 (触摸或按键)
                    for (uint i = 0; i < event.available_multipress_events; ++i) {
                        UIMultipressEvent* sub_event = &event.multipress_events[i];

                        // 处理触摸事件
                        if (sub_event->type == UI_EVENT_TYPE_TOUCH_BEGIN || sub_event->type == UI_EVENT_TYPE_TOUCH_MOVE) {
                            ProcessTouchEvent(sub_event->touch_x, sub_event->touch_y, sub_event, currentTime);
                        }
                        // 处理触摸结束事件
                        else if (sub_event->type == UI_EVENT_TYPE_TOUCH_END) {
                            // 使用特定参数处理触摸结束
                            ProcessTouchEvent(0x7FFFFFFF, 0, sub_event, currentTime);
                        }
                    }
                    // 处理完后直接进入下一次循环
                    continue;
                }
            }

            // 如果定时器未超时，或没有定时器，则继续处理其他逻辑
            for (uint i = 0; i < event.available_multipress_events; ++i) {
                UIMultipressEvent* sub_event = &event.multipress_events[i];

                // 如果子事件是“按键弹起”
                if (sub_event->type == UI_EVENT_TYPE_KEY_UP) {
                    int item_status = GetItemStatus(sub_event->key_code0);

                    if (g_TimerState->timer_handle == -1) {
                        // 如果全局定时器未激活，并且按下的键是 '.' (KEY_DOT, 0x2e)
                        if (item_status == KEY_DOT) {
                            // 检查另一个定时器是否超时
                            if (currentTime > g_AnotherTimerHandle) {
                                // **触发一个5分钟的闲置定时器**
                                CancelAndCleanupTimer();
                                int new_timer_handle = SetTimer(&g_AnotherTimerHandle, 300000, FUN_3013113c, 0); // 300000ms = 5 minutes
                                g_TimerState->timer_handle = new_timer_handle;

                                // 重置一些标志位
                                g_Flag1 = 0;
                                g_Flag2 = 0;
                                NotifyContext(g_NetworkContext);

                                // 设置500个tick后的超时时间戳
                                g_TimerState->timeout_timestamp = GetCurrentTimeTicks() + 500;

                                // 跳出整个 do-while 循环，可能是为了重启或进入特殊模式
                                goto end_loop;
                            }
                        }
                    }
                    else if (item_status != -1) {
                        // 如果定时器已激活且项目状态有效，释放定时器句柄
                        ReleaseTimerHandle(g_TimerState->timer_handle);
                    }
                }
                // 如果子事件是“按键按下”
                else if (g_TimerState->timer_handle != -1 && sub_event->type == UI_EVENT_TYPE_KEY) {
                    int key_status = GetItemStatus(sub_event->key_code0);
                    if (key_status != -1) {
                        ReleaseItem(key_status); // 释放按键

                        // 准备并发送一个网络命令
                        struct {
                            int code;
                            int key_val;
                        } cmd_data;

                        cmd_data.code = 1;
                        cmd_data.key_val = key_status & 0xFF;
                        SendNetworkCommand(g_NetworkContext, g_SomeNetworkData, &cmd_data);
                    }
                }
            }
            break; // 结束 case EV_SYSTEM_TIMER
        }

        // --- CASE: 高层键盘按下事件 ---
        case EV_KEY_PRESS: // 0x100
        {
            ResetIdleTimerOrRefreshUI();

            // 根据按键码执行不同操作
            // 使用 keycode_e 枚举中的值进行比较
            // 注意: 0x2b ('+') 和 0x2d ('-') 在提供的枚举中没有直接定义，但其ASCII值被使用
            switch (event.key_code0) {
            case 0x2b: // '+' Key, could be Volume Up or Channel Up
            case KEY_DASH: // '-' Key, could be Volume Down or Channel Down
                g_VolumeOrChannelFlag = 1;
                break;

            case KEY_COMMA: // ',' Key
            case KEY_DOLLAR: // '$' Key
                HandleVolumeOrChannelDown();
                g_VolumeOrChannelFlag = 0;
                break;
            }

            // 如果注册了按键回调函数，则调用它
            if (g_KeyPressCallback != NULL) {
                (**g_KeyPressCallback)(*g_KeyPressCallback, 0x3000, event.key_code0);
            }
            break;
        }

        // --- CASE: 系统通知/重绘事件 ---
        case EV_SYSTEM_NOTIFY: // 0x4000
            ResetIdleTimerOrRefreshUI();
            break;

            // --- DEFAULT CASE ---
        default:
            // 处理其他未明确列出的事件
            break;
        }
    } while (true);

end_loop:; // 只有在设置5分钟定时器时才会跳转到这里
}