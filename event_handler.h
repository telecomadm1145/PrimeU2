// =========================================================================
//                  ���Ͷ��� (�������ṩ��ͷ�ļ�)
// =========================================================================
#include "your_header_file.h" // ����ͷ�ļ���Ϊ��
// struct ui_event_prime_s, struct UIMultipressEvent, enum ui_event_type_e,
// enum keycode_e �ȶ������ڴ��ļ��С�

// =========================================================================
//              ȫ�ֱ����ͺ��������� (�������ǵķ���)
// =========================================================================

// --- ȫ�ֱ��� ---
// DAT_30cad054: һ��������ʱ��״̬��ȫ�ֽṹ��
struct {
    /* ... other fields ... */
    undefined4 unknown_fields[11];
    int timer_handle;          // [0xb] ��ʱ�����, -1��ʾ��Ч
    unsigned int timeout_timestamp; // [0xc] ��ʱ�����ڵ�ʱ���
} *g_TimerState = (void*)0x30cad054;

// ����ȫ��״̬
#define g_AnotherTimerHandle (*(int*)0x30cad068)
#define g_NetworkContext (*(void**)0x30cad060)
#define g_SomeNetworkData (*(undefined4*)(*(int*)0x30cad064 + 0x74))
#define g_Flag1 (*(int*)0x30cab120)
#define g_Flag2 (*(int*)0x30cab124)
#define g_VolumeOrChannelFlag (*(int*)0x30cab060)
#define g_KeyPressCallback (*(void (***)(void*, int, ...))0x30caff5c)

// --- ���� ---
#define GetEvent(event_ptr) GetEvent(event_ptr) // ϵͳAPI������������
#define GetCurrentTimeTicks() thunk_FUN_3011f3d4() // ��ȡ��ǰϵͳʱ��/�δ���
#define ProcessTouchEvent(x, y, event, time) FUN_302f3a70(x, y, event, time)
#define GetItemStatus(id) FUN_30105ecc(id) // ��ȡĳ����Ŀ/������״̬
#define ReleaseItem(id) FUN_30105ef4(id)   // �ͷ�ĳ����Ŀ/����
#define CancelAndCleanupTimer() FUN_3013a5cc()
#define SetTimer(handle_ptr, duration_ms, callback, arg) FUN_3012fd84(handle_ptr, duration_ms, callback, arg)
#define ReleaseTimerHandle(handle) FUN_3012c800(handle)
#define NotifyContext(context) FUN_30133350(context)
#define SendNetworkCommand(context, ...) FUN_3012d564(context, __VA_ARGS__)
#define ResetIdleTimerOrRefreshUI() FUN_3032a334()
#define HandleVolumeOrChannelDown() FUN_3031ed2c()

// --- δ֪/�ڲ��¼����� (���� ui_event_type_e ��) ---
#define EV_SYSTEM_TIMER   0xf        // ϵͳ��ʱ���¼�
#define EV_KEY_PRESS      0x100      // �߲���̰����¼�
#define EV_SYSTEM_NOTIFY  0x4000     // ϵͳ֪ͨ/�ػ��¼�
#define EV_TIMER_AND_KEYUP 0x100010  // ��ʱ���Ͱ������𸴺��¼�

/**
 * @brief ���¼�ѭ��������
 * @details
 *  ���������Ӧ�ó���ĺ��ģ�����һ������ѭ���еȴ�����������ϵͳ�ĸ����¼���
 *  ������ʱ�����û����루��������������ϵͳ֪ͨ��
 *
 * @param param_1 (unused) δʹ�õĲ���
 * @param param_2 (unused) δʹ�õĲ���
 * @param param_3 (unused) δʹ�õĲ������ں����ڱ�������ʱָ��
 */
void MainEventLoop(undefined4 param_1, undefined4 param_2, undefined4* param_3)
{
    struct ui_event_prime_s event;
    unsigned int currentTime;

    // ���¼�ѭ��
    do {
        // 1. �ȴ�����ȡ��һ���¼�
        GetEvent(&event);
        currentTime = GetCurrentTimeTicks();

        // 2. �������¼�����ַ��¼�
        // ע�⣺����ʹ�õ���ϵͳ���¼����룬���� ui_event_type_e ö��
        switch (event.event_type) {

            // --- CASE: ϵͳ��ʱ���򸴺��¼� ---
        case EV_SYSTEM_TIMER:    // 0xf
        case EV_TIMER_AND_KEYUP: // 0x100010
        {
            // ���ȫ�ֶ�ʱ���Ƿ��ѹ���
            // �����������ɵ� `x + 1 < 0 == SCARRY4(x, 1)` �� `x != -1` �ĸ���д��
            if (g_TimerState->timer_handle != -1) {
                // �����ǰʱ�䳬�����趨�ĳ�ʱʱ���
                if (currentTime > g_TimerState->timeout_timestamp) {
                    // ��ʱ���ѳ�ʱ�����ö�ʱ��״̬
                    g_TimerState->timeout_timestamp = 0;

                    // �����¼��е���������Ŀ (�����򰴼�)
                    for (uint i = 0; i < event.available_multipress_events; ++i) {
                        UIMultipressEvent* sub_event = &event.multipress_events[i];

                        // �������¼�
                        if (sub_event->type == UI_EVENT_TYPE_TOUCH_BEGIN || sub_event->type == UI_EVENT_TYPE_TOUCH_MOVE) {
                            ProcessTouchEvent(sub_event->touch_x, sub_event->touch_y, sub_event, currentTime);
                        }
                        // �����������¼�
                        else if (sub_event->type == UI_EVENT_TYPE_TOUCH_END) {
                            // ʹ���ض���������������
                            ProcessTouchEvent(0x7FFFFFFF, 0, sub_event, currentTime);
                        }
                    }
                    // �������ֱ�ӽ�����һ��ѭ��
                    continue;
                }
            }

            // �����ʱ��δ��ʱ����û�ж�ʱ������������������߼�
            for (uint i = 0; i < event.available_multipress_events; ++i) {
                UIMultipressEvent* sub_event = &event.multipress_events[i];

                // ������¼��ǡ���������
                if (sub_event->type == UI_EVENT_TYPE_KEY_UP) {
                    int item_status = GetItemStatus(sub_event->key_code0);

                    if (g_TimerState->timer_handle == -1) {
                        // ���ȫ�ֶ�ʱ��δ������Ұ��µļ��� '.' (KEY_DOT, 0x2e)
                        if (item_status == KEY_DOT) {
                            // �����һ����ʱ���Ƿ�ʱ
                            if (currentTime > g_AnotherTimerHandle) {
                                // **����һ��5���ӵ����ö�ʱ��**
                                CancelAndCleanupTimer();
                                int new_timer_handle = SetTimer(&g_AnotherTimerHandle, 300000, FUN_3013113c, 0); // 300000ms = 5 minutes
                                g_TimerState->timer_handle = new_timer_handle;

                                // ����һЩ��־λ
                                g_Flag1 = 0;
                                g_Flag2 = 0;
                                NotifyContext(g_NetworkContext);

                                // ����500��tick��ĳ�ʱʱ���
                                g_TimerState->timeout_timestamp = GetCurrentTimeTicks() + 500;

                                // �������� do-while ѭ����������Ϊ���������������ģʽ
                                goto end_loop;
                            }
                        }
                    }
                    else if (item_status != -1) {
                        // �����ʱ���Ѽ�������Ŀ״̬��Ч���ͷŶ�ʱ�����
                        ReleaseTimerHandle(g_TimerState->timer_handle);
                    }
                }
                // ������¼��ǡ��������¡�
                else if (g_TimerState->timer_handle != -1 && sub_event->type == UI_EVENT_TYPE_KEY) {
                    int key_status = GetItemStatus(sub_event->key_code0);
                    if (key_status != -1) {
                        ReleaseItem(key_status); // �ͷŰ���

                        // ׼��������һ����������
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
            break; // ���� case EV_SYSTEM_TIMER
        }

        // --- CASE: �߲���̰����¼� ---
        case EV_KEY_PRESS: // 0x100
        {
            ResetIdleTimerOrRefreshUI();

            // ���ݰ�����ִ�в�ͬ����
            // ʹ�� keycode_e ö���е�ֵ���бȽ�
            // ע��: 0x2b ('+') �� 0x2d ('-') ���ṩ��ö����û��ֱ�Ӷ��壬����ASCIIֵ��ʹ��
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

            // ���ע���˰����ص��������������
            if (g_KeyPressCallback != NULL) {
                (**g_KeyPressCallback)(*g_KeyPressCallback, 0x3000, event.key_code0);
            }
            break;
        }

        // --- CASE: ϵͳ֪ͨ/�ػ��¼� ---
        case EV_SYSTEM_NOTIFY: // 0x4000
            ResetIdleTimerOrRefreshUI();
            break;

            // --- DEFAULT CASE ---
        default:
            // ��������δ��ȷ�г����¼�
            break;
        }
    } while (true);

end_loop:; // ֻ��������5���Ӷ�ʱ��ʱ�Ż���ת������
}