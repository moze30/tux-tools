/*
 * Tux-Tools - interface language, implementation.
 *
 * Chinese is the source language, so a Chinese string is its own key and the
 * table below maps it to English. Anything missing from the table falls back to
 * the key, which keeps a half-finished translation readable rather than blank.
 *
 * The environment decides the default: the user's Windows UI language selects
 * the concrete language at startup, and the settings card can override it.
 * "Follow system" is offered as an explicit choice as well, and it re-resolves
 * from the environment every time it is picked, so a copy of the program moved
 * to another machine follows that machine.
 */

#include "i18n.h"

#include "settings.h"

namespace i18n {

namespace {

struct Entry {
    const wchar_t* zh;
    const wchar_t* en;
};

// Kept as a flat array rather than a map: there are a few hundred entries and it
// is read far more often than it is written, so a linear scan over a contiguous
// array beats hashing for this size and needs no allocation.
const Entry kTable[] = {
    {L"===== 程序正常退出 =====", L"===== Tux-Tools exited normally ====="},
    {L"===== 调试日志已启用 =====", L"===== Debug log enabled ====="},
    {L"===== 调试日志已关闭 =====", L"===== Debug log disabled ====="},
    {L"%ls 失败：%ls (0x%08lX)", L"%ls failed: %ls (0x%08lX)"},
    {L"%ls 失败：0x%08lX", L"%ls failed: 0x%08lX"},
    {L"          系统消息：%ls", L"          system message: %ls"},
    {L"成功", L"succeeded"},
    {L"失败", L"failed"},
    {L"%ls %ls：%ls (VkResult %d)", L"%ls %ls: %ls (VkResult %d)"},
    {L"%ls %ls：VkResult %d", L"%ls %ls: VkResult %d"},
    {L"---- 运行环境 ----", L"---- environment ----"},
    {L"vulkan-1.dll：未找到", L"vulkan-1.dll: not found"},
    {L"vulkan-1.dll：%ls", L"vulkan-1.dll: %ls"},
    {L"  loader 文件版本：%u.%u.%u.%u", L"  loader file version: %u.%u.%u.%u"},
    {L"Vulkan ICD：%ls", L"Vulkan ICD: %ls"},
    {L"Vulkan ICD：注册表项为空", L"Vulkan ICD: the registry key is empty"},
    {L"Vulkan ICD：注册表中没有 HKLM\\SOFTWARE\\Khronos\\Vulkan\\Drivers", L"Vulkan ICD: HKLM\\SOFTWARE\\Khronos\\Vulkan\\Drivers is not present in the registry"},
    {L"  缺少 %ls", L"  missing %ls"},
    {L"运行在 Wine 下：%ls", L"Running under Wine: %ls"},
    {L"是", L"yes"},
    {L"否", L"no"},
    {L"Wine 版本：%hs", L"Wine version: %hs"},
    {L"  环境变量 %ls=%ls", L"  environment %ls=%ls"},
    {L"---- 环境结束 ----", L"---- environment end ----"},
    {L"访问冲突（读写无效地址）", L"Access violation (read or write of an invalid address)"},
    {L"页面错误（无法读入页面）", L"Page fault (the page could not be paged in)"},
    {L"非法指令（可能是不支持的 CPU 指令）", L"Illegal instruction (possibly an unsupported CPU instruction)"},
    {L"不可继续的异常", L"Non-continuable exception"},
    {L"数组越界", L"Array bounds exceeded"},
    {L"整数除零", L"Integer divide by zero"},
    {L"整数溢出", L"Integer overflow"},
    {L"特权指令", L"Privileged instruction"},
    {L"栈溢出", L"Stack overflow"},
    {L"堆损坏", L"Heap corruption"},
    {L"无效参数（CRT 检测到）", L"Invalid parameter (detected by the CRT)"},
    {L"安全 cookie 校验失败（栈被破坏）", L"Security cookie check failed (the stack was overwritten)"},
    {L"未捕获的 C++ 异常", L"Uncaught C++ exception"},
    {L"=========== 程序崩溃了 ===========", L"=========== the program crashed ==========="},
    {L"异常：%ls (0x%08lX)", L"Exception: %ls (0x%08lX)"},
    {L"异常代码：0x%08lX", L"Exception code: 0x%08lX"},
    {L"出错地址：%p", L"Faulting address: %p"},
    {L"上面最后一条普通日志就是崩溃前正在做的事。", L"The last ordinary line above is what the program was doing when it crashed."},
    {L"出错模块：%ls", L"Faulting module: %ls"},
    {L"[选择] %ls", L"[select] %ls"},
    {L"媒体文件", L"Media files"},
    {L"所有文件", L"All files"},
    {L"选择要测试解码的媒体文件", L"Choose a media file to decode"},
    {L"[文件] %ls", L"[file] %ls"},
    {L"已选择: ", L"Selected: "},
    {L"[错误] 请先选择一个媒体文件", L"[error] choose a media file first"},
    {L"未选择文件", L"No file selected"},
    {L"[警告] 本机不支持 D3D11 视频处理，回退到软件解码", L"[warning] D3D11 video processing is unavailable here, falling back to software decoding"},
    {L"[警告] 本机不支持 D3D12 视频处理，回退到软件解码", L"[warning] D3D12 video processing is unavailable here, falling back to software decoding"},
    {L"[失败] %ls 无法打开该文件", L"[failed] %ls could not open the file"},
    {L" 打开失败", L" open failed"},
    {L" | 仅音频", L" | audio only"},
    {L"硬件加速", L"hardware accelerated"},
    {L"软件解码", L"software decoding"},
    {L" | 丢帧 %d", L" | %d dropped"},
    {L"[完成] 播放结束", L"[done] playback finished"},
    {L"[错误] 解码失败", L"[error] decoding failed"},
    {L"[错误]", L"[error]"},
    {L"[失败]", L"[failed]"},
    {L"[警告]", L"[warning]"},
    {L"WGL 扩展（系统层）", L"WGL extensions (system level)"},
    {L"状态", L"Status"},
    {L"无 WGL 扩展字符串", L"no WGL extension string"},
    {L"支持", L"supported"},
    {L"OpenGL - 无法创建上下文", L"OpenGL - could not create a context"},
    {L"无法在当前系统上创建 OpenGL 上下文。", L"An OpenGL context could not be created on this system."},
    {L"实现信息", L"Implementation"},
    {L"厂商", L"Vendor"},
    {L"渲染器", L"Renderer"},
    {L"OpenGL 版本", L"OpenGL version"},
    {L"着色语言版本", L"Shading language version"},
    {L"驱动限制", L"Driver limits"},
    {L"OpenGL 扩展", L"OpenGL extensions"},
    {L"数量", L"Count"},
    {L"压缩纹理格式", L"Compressed texture formats"},
    {L"通过 Windows 图形 API 查看驱动能力、测试驱动性能，并对比系统各条视频解码路径。", L"Inspect driver capabilities, test driver performance and compare the system's video decoding paths through the Windows graphics APIs."},
    {L"驱动能力查看器", L"Driver capability viewer"},
    {L"创建渲染宿主窗口失败（错误 %lu）", L"Could not create the render host window (error %lu)"},
    {L"驱动测试：新渲染宿主窗口 %p（%ls）", L"Driver test: new render host window %p (%ls)"},
    {L"沿用旧窗口", L"reused the old window"},
    {L"全新窗口", L"a brand new window"},
    {L"驱动测试：渲染区尺寸无效（%dx%d），跳过初始化", L"Driver test: the render area has an invalid size (%dx%d), skipping initialisation"},
    {L"驱动测试：%ls 初始化失败 - %ls", L"Driver test: %ls failed to initialise - %ls"},
    {L"驱动测试：改用顶层窗口重试 %ls", L"Driver test: retrying %ls with a top-level window"},
    {L"驱动测试：顶层窗口可用，后续后端将直接使用", L"Driver test: the top-level window works; later backends will use it directly"},
    {L"无法初始化 ", L"Could not initialise "},
    {L"：", L": "},
    {L"驱动测试：%ls 初始化成功 - %ls", L"Driver test: %ls initialised - %ls"},
    {L"驱动测试：开始 %ls", L"Driver test: starting %ls"},
    {L"驱动测试：宿主窗口 可见=%ls 置顶=%ls 位置=%ld,%ld 尺寸=%ldx%ld", L"Driver test: host window visible=%ls topmost=%ls position=%ld,%ld size=%ldx%ld"},
    {L"驱动测试：停止", L"Driver test: stopped"},
    {L"选择一种解码方式，打开媒体文件后点击开始", L"Choose a decoding method, open a media file and press start"},
    {L"驱动测试", L"Driver test"},
    {L"解码测试", L"Decode test"},
    {L"设置...", L"Settings..."},
    {L"关于 Tux-Tools...", L"About Tux-Tools..."},
    {L"切换到：驱动能力查看器", L"Switched to: driver capability viewer"},
    {L"切换到：驱动测试", L"Switched to: driver test"},
    {L"切换到：解码测试", L"Switched to: decode test"},
    {L"图形 API 测试", L"Graphics API test"},
    {L"解码方式", L"Decoding method"},
    {L"驱动测试 - ", L"Driver test - "},
    {L"停止", L"Stop"},
    {L"开始", L"Start"},
    {L"打开文件", L"Open file"},
    {L"属性", L"Property"},
    {L"驱动信息已就绪", L"Driver information is ready"},
    {L"版本 ", L"Version "},
    {L"仓库", L"Repository"},
    {L"作者", L"Author"},
    {L"许可证", L"License"},
    {L"鸣谢", L"Credits"},
    {L"设置", L"Settings"},
    {L"字体大小", L"Text size"},
    {L"重置", L"Reset"},
    {L"调整整个界面的文字大小，立即生效并记住选择。", L"Scales all interface text. Applies at once and is remembered."},
    {L"调试日志", L"Debug log"},
    {L"已启用。程序会把运行情况写入下面的文件，便于排查问题。", L"Enabled. What the program does is written to the file below."},
    {L"启用后会在程序所在目录持续写入日志，便于排查问题。", L"When enabled, a log is written next to the program."},
    {L"设置：调试日志 %ls", L"Settings: debug log %ls"},
    {L"开", L"on"},
    {L"关", L"off"},
    {L"界面语言", L"Language"},
    {L"默认跟随系统语言，切换后立即生效并记住选择。", L"Follows the system language. A change applies at once."},
    {L"跟随系统", L"Follow system"},
    {L"===== Tux-Tools 启动 =====", L"===== Tux-Tools started ====="},
    {L"界面文字大小：%d%%", L"Interface text size: %d%%"},
    {L"界面语言：%hs", L"Interface language: %hs"},
    {L"命令行：%ls", L"Command line: %ls"},
    {L"COM 初始化：0x%08lx", L"COM initialisation: 0x%08lx"},
    {L"未知", L"unknown"},
    {L"系统版本：%ls（kernel32 %ls）", L"System version: %ls (kernel32 %ls)"},
    {L"处理器架构：%ls", L"Processor architecture: %ls"},
    {L"主循环结束，准备退出", L"Message loop finished, exiting"},
    {L"未知原因", L"unknown reason"},
    {L"Direct3D 10：创建设备与交换链（硬件与 WARP 均失败）", L"Direct3D 10: creating the device and swap chain (hardware and WARP both failed)"},
    {L"Direct3D 10：创建设备成功（%ls）", L"Direct3D 10: device created (%ls)"},
    {L"硬件", L"hardware"},
    {L"Direct3D 10：创建顶点着色器", L"Direct3D 10: creating the vertex shader"},
    {L"Direct3D 10：创建像素着色器", L"Direct3D 10: creating the pixel shader"},
    {L"Direct3D 10：上传球体失败", L"Direct3D 10: uploading the sphere failed"},
    {L"Direct3D 10：  球体 %u 顶点 %u 索引", L"Direct3D 10:   sphere %u vertices %u indices"},
    {L"Direct3D 10：上传圆环失败", L"Direct3D 10: uploading the ring failed"},
    {L"Direct3D 10：  圆环 %u 顶点 %u 索引", L"Direct3D 10:   ring %u vertices %u indices"},
    {L"Direct3D 10：上传卫星失败", L"Direct3D 10: uploading the moon failed"},
    {L"Direct3D 10：  卫星 %u 顶点 %u 索引", L"Direct3D 10:   moon %u vertices %u indices"},
    {L"Direct3D 10：上传公告板失败", L"Direct3D 10: uploading the billboard failed"},
    {L"Direct3D 10：  公告板 %u 顶点 %u 索引", L"Direct3D 10:   billboard %u vertices %u indices"},
    {L"Direct3D 10：  行星纹理 %ux%u，%u 字节", L"Direct3D 10:   planet texture %ux%u, %u bytes"},
    {L"Direct3D 10：  光环纹理 %ux%u，%u 字节", L"Direct3D 10:   ring texture %ux%u, %u bytes"},
    {L"Direct3D 10：  星点纹理 %ux%u，%u 字节", L"Direct3D 10:   star texture %ux%u, %u bytes"},
    {L"Direct3D 10：第 %d 张纹理数据不足（需要 %u 字节，实际 %u 字节）", L"Direct3D 10: texture %d has too little data (needs %u bytes, has %u)"},
    {L"Direct3D 10：上传第 %d 张纹理失败（%dx%d）", L"Direct3D 10: uploading texture %d failed (%dx%d)"},
    {L"Direct3D 10：为一纹理创建视图", L"Direct3D 10: creating a view for a texture"},
    {L"Direct3D 10：  第 %d 张纹理已上传（%dx%d）", L"Direct3D 10:   texture %d uploaded (%dx%d)"},
    {L"Direct3D 10：开始初始化", L"Direct3D 10: starting initialisation"},
    {L"无法创建设备", L"Could not create the device"},
    {L"Direct3D 10：设备已建立，%ls", L"Direct3D 10: device established, %ls"},
    {L"无法编译着色器", L"Could not compile the shader"},
    {L"Direct3D 10：着色器已编译", L"Direct3D 10: shaders compiled"},
    {L"无法创建几何缓冲", L"Could not create the geometry buffers"},
    {L"Direct3D 10：几何缓冲已创建", L"Direct3D 10: geometry buffers created"},
    {L"无法创建纹理", L"Could not create the textures"},
    {L"Direct3D 10：纹理已创建", L"Direct3D 10: textures created"},
    {L"无法创建渲染状态", L"Could not create the render states"},
    {L"Direct3D 10：渲染状态已创建", L"Direct3D 10: render states created"},
    {L"无法创建阴影贴图", L"Could not create the shadow map"},
    {L"Direct3D 10：阴影贴图已创建", L"Direct3D 10: shadow map created"},
    {L"Direct3D 10：尺寸相关资源已创建", L"Direct3D 10: size-dependent resources created"},
    {L"渲染目标视图为空", L"The render target view is null"},
    {L"Direct3D 10：初始化完成", L"Direct3D 10: initialisation complete"},
    {L"%ls：编译器未返回诊断信息", L"%ls: the compiler returned no diagnostics"},
    {L"%ls：编译器诊断信息为空", L"%ls: the compiler diagnostics are empty"},
    {L"%ls 编译失败：", L"%ls failed to compile:"},
    {L"Direct3D 11：创建设备与交换链（硬件与 WARP 均失败）", L"Direct3D 11: creating the device and swap chain (hardware and WARP both failed)"},
    {L"无法创建 D3D11 设备", L"Could not create the D3D11 device"},
    {L"Direct3D 11：编译场景顶点着色器", L"Direct3D 11: compiling the scene vertex shader"},
    {L"场景顶点着色器", L"scene vertex shader"},
    {L"Direct3D 11：编译场景像素着色器", L"Direct3D 11: compiling the scene pixel shader"},
    {L"场景像素着色器", L"scene pixel shader"},
    {L"Direct3D 11：编译全屏顶点着色器", L"Direct3D 11: compiling the full-screen vertex shader"},
    {L"全屏顶点着色器", L"full-screen vertex shader"},
    {L"Direct3D 11：编译后处理通道 %hs 失败", L"Direct3D 11: compiling post-processing pass %hs failed"},
    {L"Direct3D 11：编译后处理着色器", L"Direct3D 11: compiling the post-processing shaders"},
    {L"后处理着色器", L"post-processing shader"},
    {L"Direct3D 11：开始初始化", L"Direct3D 11: starting initialisation"},
    {L"Direct3D 11：设备已建立，%ls", L"Direct3D 11: device established, %ls"},
    {L"Direct3D 11：着色器已编译", L"Direct3D 11: shaders compiled"},
    {L"无法创建全屏四边形", L"Could not create the full-screen quad"},
    {L"无法创建离屏目标", L"Could not create the off-screen target"},
    {L"Direct3D 11：初始化完成", L"Direct3D 11: initialisation complete"},
    {L"Direct3D 12：CreateDXGIFactory1(IDXGIFactory4)", L"Direct3D 12: CreateDXGIFactory1(IDXGIFactory4)"},
    {L"Direct3D 12：CreateSwapChainForHwnd（flip 模型）", L"Direct3D 12: CreateSwapChainForHwnd (flip model)"},
    {L"Direct3D 12：窗口 style=0x%08lX (WS_CHILD=%ls) parent=%p root=%p", L"Direct3D 12: window style=0x%08lX (WS_CHILD=%ls) parent=%p root=%p"},
    {L"Direct3D 12：交换链参数 width=%u height=%u format=RGBA8 buffers=%d swapeffect=FLIP_DISCARD 适配器=%ls", L"Direct3D 12: swap chain width=%u height=%u format=RGBA8 buffers=%d swapeffect=FLIP_DISCARD adapter=%ls"},
    {L"Direct3D 12：D3D12CreateDevice", L"Direct3D 12: D3D12CreateDevice"},
    {L"Direct3D 12：CreateCommandQueue", L"Direct3D 12: CreateCommandQueue"},
    {L"Direct3D 12：开始初始化", L"Direct3D 12: starting initialisation"},
    {L"无法创建 D3D12 设备", L"Could not create the D3D12 device"},
    {L"无法创建命令队列", L"Could not create the command queue"},
    {L"Direct3D 12：设备与队列已建立", L"Direct3D 12: device and queue established"},
    {L"无法创建交换链", L"Could not create the swap chain"},
    {L"无法创建根签名", L"Could not create the root signature"},
    {L"无法创建管线", L"Could not create the pipeline"},
    {L"无法创建帧资源", L"Could not create the frame resources"},
    {L"无法创建渲染目标", L"Could not create the render target"},
    {L"Direct3D 12：初始化完成", L"Direct3D 12: initialisation complete"},
    {L"Direct3D 9：Direct3DCreate9 返回空（d3d9.dll 存在但无法提供 IDirect3D9）", L"Direct3D 9: Direct3DCreate9 returned null (d3d9.dll is present but offers no IDirect3D9)"},
    {L"Direct3D 9：CreateDevice（硬件/混合/软件三种顶点处理均失败）", L"Direct3D 9: CreateDevice (hardware, mixed and software vertex processing all failed)"},
    {L"Direct3D 9：CreateDevice 成功（%ls）", L"Direct3D 9: CreateDevice succeeded (%ls)"},
    {L"Direct3D 9：开始初始化", L"Direct3D 9: starting initialisation"},
    {L"无法创建 D3D9 设备", L"Could not create the D3D9 device"},
    {L"Direct3D 9：设备已建立，%ls", L"Direct3D 9: device established, %ls"},
    {L"Direct3D 9：初始化完成", L"Direct3D 9: initialisation complete"},
    {L"OpenGL：ChoosePixelFormat 未返回可用格式（驱动不支持 OpenGL 像素格式）", L"OpenGL: ChoosePixelFormat returned no usable format (the driver supports no OpenGL pixel format)"},
    {L"OpenGL：SetPixelFormat 失败（格式 %d，错误 %lu）", L"OpenGL: SetPixelFormat failed (format %d, error %lu)"},
    {L"OpenGL：wglCreateContext 失败（错误 %lu）", L"OpenGL: wglCreateContext failed (error %lu)"},
    {L"OpenGL：wglMakeCurrent 失败（错误 %lu）", L"OpenGL: wglMakeCurrent failed (error %lu)"},
    {L"OpenGL：上下文已建立 - %hs / %hs", L"OpenGL: context established - %hs / %hs"},
    {L"OpenGL：开始创建上下文", L"OpenGL: creating the context"},
    {L"无法创建 WGL 上下文（OpenGL 不可用）", L"Could not create the WGL context (OpenGL is unavailable)"},
    {L"OpenGL：makeContext 失败 - ChoosePixelFormat / SetPixelFormat / wglCreateContext / wglMakeCurrent 之一未成功", L"OpenGL: makeContext failed - one of ChoosePixelFormat, SetPixelFormat, wglCreateContext or wglMakeCurrent did not succeed"},
    {L"Vulkan：实例不支持 VK_KHR_surface", L"Vulkan: the instance does not support VK_KHR_surface"},
    {L"Vulkan：实例不支持 VK_KHR_win32_surface（该环境的 Vulkan 无法向窗口呈现，此项不可用）", L"Vulkan: the instance does not support VK_KHR_win32_surface (this environment's Vulkan cannot present to a window, so this is unavailable)"},
    {L"Vulkan：实例扩展可用（VK_KHR_surface、VK_KHR_win32_surface）", L"Vulkan: the instance extensions are available (VK_KHR_surface, VK_KHR_win32_surface)"},
    {L"Vulkan：实例扩展共 %u 个：", L"Vulkan: %u instance extensions in total:"},
    {L"Vulkan：VK_KHR_portability_enumeration %ls", L"Vulkan: VK_KHR_portability_enumeration %ls"},
    {L"可用", L"available"},
    {L"不可用", L"unavailable"},
    {L"Vulkan：实例未提供 vkCreateWin32SurfaceKHR（缺少 VK_KHR_win32_surface 扩展，无法向窗口呈现）", L"Vulkan: the instance offers no vkCreateWin32SurfaceKHR (the VK_KHR_win32_surface extension is missing, so it cannot present to a window)"},
    {L"Vulkan：交给 vkCreateWin32SurfaceKHR 的窗口 - handle=%p", L"Vulkan: the window handed to vkCreateWin32SurfaceKHR - handle=%p"},
    {L"Vulkan：  style=0x%08lX (WS_CHILD=%ls) exstyle=0x%08lX", L"Vulkan:   style=0x%08lX (WS_CHILD=%ls) exstyle=0x%08lX"},
    {L"Vulkan：  parent=%p root=%p（相同则说明是顶层窗口）", L"Vulkan:   parent=%p root=%p (equal means a top-level window)"},
    {L"Vulkan：  窗口矩形 %ldx%ld，客户区 %ldx%ld，可见=%ls，最小化=%ls", L"Vulkan:   window rect %ldx%ld, client %ldx%ld, visible=%ls, minimised=%ls"},
    {L"Vulkan：vkCreateWin32SurfaceKHR 未成功", L"Vulkan: vkCreateWin32SurfaceKHR did not succeed"},
    {L"Vulkan：Win32 表面已创建", L"Vulkan: the Win32 surface was created"},
    {L"Vulkan：物理设备 %u 个", L"Vulkan: %u physical devices"},
    {L"Vulkan：  [%u] %hs（类型 %d，API %u.%u.%u）", L"Vulkan:   [%u] %hs (type %d, API %u.%u.%u)"},
    {L"Vulkan：      队列族 %u 支持图形；呈现支持查询=%d，结果=%ls", L"Vulkan:      queue family %u supports graphics; present-support query=%d, result=%ls"},
    {L"查询失败", L"the query failed"},
    {L"支持呈现", L"present supported"},
    {L"不支持呈现", L"present not supported"},
    {L"Vulkan：选中设备 %u 的队列族 %u", L"Vulkan: chose queue family %u of device %u"},
    {L"Vulkan：没有任何设备同时支持图形与向该表面呈现", L"Vulkan: no device supports both graphics and presenting to that surface"},
    {L"Vulkan：开始初始化", L"Vulkan: starting initialisation"},
    {L"无法加载 vulkan-1.dll", L"Could not load vulkan-1.dll"},
    {L"Vulkan：加载 vulkan-1.dll 或解析 vkGetInstanceProcAddr 失败", L"Vulkan: loading vulkan-1.dll or resolving vkGetInstanceProcAddr failed"},
    {L"Vulkan：loader 已加载", L"Vulkan: loader loaded"},
    {L"Vulkan：vulkan-1.dll = %ls", L"Vulkan: vulkan-1.dll = %ls"},
    {L"Vulkan：loader 实例版本 %u.%u.%u", L"Vulkan: loader instance version %u.%u.%u"},
    {L"无法创建 Vulkan 实例", L"Could not create the Vulkan instance"},
    {L"无法解析 Vulkan 入口点", L"Could not resolve the Vulkan entry points"},
    {L"无法创建 Win32 表面", L"Could not create the Win32 surface"},
    {L"找不到合适的物理设备", L"No suitable physical device was found"},
    {L"Vulkan：实例、表面与物理设备已就绪", L"Vulkan: instance, surface and physical device are ready"},
    {L"无法创建逻辑设备", L"Could not create the logical device"},
    {L"无法创建命令池", L"Could not create the command pool"},
    {L"无法创建渲染通道", L"Could not create the render pass"},
    {L"无法创建描述符", L"Could not create the descriptors"},
    {L"无法创建资源", L"Could not create the resources"},
    {L"无法创建同步对象", L"Could not create the synchronisation objects"},
    {L"搜索属性...", L"Search properties..."},
    {L"值", L"Value"},
    {L"未知厂商", L"Unknown vendor"},
    {L"集成显卡", L"Integrated GPU"},
    {L"独立显卡", L"Discrete GPU"},
    {L"虚拟显卡", L"Virtual GPU"},
    {L"其他", L"Other"},
    {L"AMD 专有驱动", L"AMD proprietary driver"},
    {L"AMD 开源驱动", L"AMD open source driver"},
    {L"NVIDIA 专有驱动", L"NVIDIA proprietary driver"},
    {L"Intel 专有驱动（Windows）", L"Intel proprietary driver (Windows)"},
    {L"Intel 开源驱动（Mesa）", L"Intel open source driver (Mesa)"},
    {L"Imagination 专有驱动", L"Imagination proprietary driver"},
    {L"Qualcomm 专有驱动", L"Qualcomm proprietary driver"},
    {L"ARM 专有驱动", L"ARM proprietary driver"},
    {L"GGP 专有驱动", L"GGP proprietary driver"},
    {L"Broadcom 专有驱动", L"Broadcom proprietary driver"},
    {L"CoreAVI 专有驱动", L"CoreAVI proprietary driver"},
    {L"Juice 专有驱动", L"Juice proprietary driver"},
    {L"VeriSilicon 专有驱动", L"VeriSilicon proprietary driver"},
    {L"Samsung 专有驱动", L"Samsung proprietary driver"},
    {L"Imagination 开源驱动（Mesa）", L"Imagination open source driver (Mesa)"},
    {L"Vulkan SC 模拟层（基于 Vulkan）", L"Vulkan SC emulation layer (over Vulkan)"},
    {L"未知驱动", L"Unknown driver"},
    {L"设备特性（已支持）", L"Device features (supported)"},
    {L"（无）", L"(none)"},
    {L"设备限制", L"Device limits"},
    {L"加载器", L"Loader"},
    {L"Vulkan 加载器版本", L"Vulkan loader version"},
    {L"1.0（加载器未上报版本）", L"1.0 (the loader reported no version)"},
    {L"实例扩展", L"Instance extensions"},
    {L"规范版本 ", L"Spec version "},
    {L"实例层", L"Instance layers"},
    {L"，实现版本 ", L", implementation version "},
    {L"Vulkan - 未找到加载器", L"Vulkan - no loader found"},
    {L"未在当前系统上找到 vulkan-1.dll（Vulkan 加载器）。", L"vulkan-1.dll (the Vulkan loader) was not found on this system."},
    {L"Vulkan - 实例创建失败", L"Vulkan - instance creation failed"},
    {L"创建 Vulkan 实例失败（VkResult ", L"Creating the Vulkan instance failed (VkResult "},
    {L"）。", L")."},
    {L"Vulkan - 无可用设备", L"Vulkan - no usable device"},
    {L"Vulkan 设备", L"Vulkan devices"},
    {L"未找到任何 Vulkan 物理设备。", L"No Vulkan physical device was found."},
    {L"设备 ", L"Device "},
    {L"设备名称", L"Device name"},
    {L"设备类型", L"Device type"},
    {L"厂商 ID", L"Vendor ID"},
    {L"设备 ID", L"Device ID"},
    {L"API 版本", L"API version"},
    {L"驱动版本（原始值）", L"Driver version (raw)"},
    {L"驱动版本（NVIDIA）", L"Driver version (NVIDIA)"},
    {L"驱动版本（Intel）", L"Driver version (Intel)"},
    {L"驱动名称", L"Driver name"},
    {L"驱动（上报名称）", L"Driver (reported name)"},
    {L"驱动信息", L"Driver information"},
    {L"一致性规范版本", L"Conformance spec version"},
    {L"队列族", L"Queue families"},
    {L"队列族 ", L"Queue family "},
    {L"，数量 ", L", count "},
    {L"，timestampValidBits ", L", timestampValidBits "},
    {L"内存堆", L"Memory heaps"},
    {L"堆 ", L"Heap "},
    {L"内存类型", L"Memory types"},
    {L"类型 ", L"Type "},
    {L"设备扩展（", L"Device extensions ("},
    {L"）", L")"},
};

const int kTableSize = static_cast<int>(sizeof(kTable) / sizeof(kTable[0]));

// The languages the settings card lists, in display order. Only the languages
// themselves: the environment is what decides the default, so there is no
// separate "follow system" entry to offer - on first run the highlighted entry
// is already the environment's own language.
const LanguageInfo kLanguages[] = {
    {Language::Chinese, L"简体中文"},
    {Language::English, L"English"},
};

static_assert(sizeof(kLanguages) / sizeof(kLanguages[0]) == kLanguageCount,
              "kLanguageCount and kLanguages disagree");

Language g_active = Language::Chinese;  // what tr() uses

// The translated text of the most recent lookup. tr() hands out a pointer rather
// than an object so it can be passed straight to the Win32 APIs that want an
// LPWSTR; keeping one buffer here means the pointer stays valid until the next
// call, which is what every existing call site already assumes of a literal.
std::wstring g_buffer;

const wchar_t* asText(Language lang) {
    return lang == Language::English ? L"en-US" : L"zh-CN";
}

// Resolves a stored value. Only two values are ever written; anything else -
// including the empty string a first-run INI has no entry for - means the user
// has not chosen, and the environment decides.
bool fromText(const std::wstring& text, Language* out) {
    if (text == L"en-US" || text == L"en" || text == L"english") {
        *out = Language::English;
        return true;
    }
    if (text == L"zh-CN" || text == L"zh" || text == L"chinese") {
        *out = Language::Chinese;
        return true;
    }
    return false;
}

}  // namespace

// The environment's language.
//
// GetUserDefaultUILanguage is the right call for an interface: it is the user's
// preferred UI language rather than the system's installation language, and Wine
// implements it from the locale it was started with. Anything that is not
// Chinese resolves to English, which is the safer default for a reader whose
// language is not offered.
Language detect() {
    const LANGID lang = GetUserDefaultUILanguage();
    const WORD primary = PRIMARYLANGID(lang);
    if (primary == LANG_CHINESE) return Language::Chinese;
    return Language::English;
}

const std::vector<LanguageInfo>& languages() {
    static const std::vector<LanguageInfo> list(
        kLanguages, kLanguages + sizeof(kLanguages) / sizeof(kLanguages[0]));
    return list;
}

Language language() { return g_active; }
Language selection() { return g_active; }

void setLanguage(Language value) {
    g_active = value;
    // Saved in the same INI as the rest of the preferences.
    settings::saveLanguage(asText(value));
}

std::string code() {
    const wchar_t* text = asText(g_active);
    std::string out;
    for (const wchar_t* p = text; *p; ++p) out.push_back(static_cast<char>(*p));
    return out;
}

const wchar_t* tr(const wchar_t* zh) {
    if (!zh) return L"";
    if (g_active == Language::English) {
        for (int i = 0; i < kTableSize; ++i) {
            if (wcscmp(kTable[i].zh, zh) == 0) return kTable[i].en;
        }
    }
    if (g_active == Language::Chinese) return zh;
    // An unknown string in a language other than Chinese: show the original
    // rather than an empty label.
    return zh;
}

// Called by settings once the INI has been located, so the saved choice - if
// there is one - is in force before the first window is created. With no saved
// choice the environment decides, which is what makes a fresh copy open in the
// language of the machine it is on.
void loadSaved(const std::wstring& saved) {
    Language stored = Language::Chinese;
    if (fromText(saved, &stored)) {
        g_active = stored;
    } else {
        g_active = detect();
    }
}

}  // namespace i18n
