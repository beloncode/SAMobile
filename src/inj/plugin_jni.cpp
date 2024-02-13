#include <sched.h>
#include <thread>
#include <signal.h>
#include <sys/ucontext.h>
#include <cstdlib>

#include <core/log_client.h>
#include <core/main_thread.h>

#include <inj/patches_level.h>
#include <texture_runtime.h>
#include <ui/user_graphics.h>

#include <inj/jvm_helper.h>
#include <game/linux_hierarchy.h>

// This env is specific by the calling thread and shouldn't be shared
JNIEnv* g_gameEnv;
jobject g_gtaSA;
// Game engine, base address
uintptr_t g_gameAddr;
// OpenAL's address space, we will use later to direct produce sound wout contact the game engine
// will be useful when a sound overlay is needed
uintptr_t g_audioBackend;

extern AArch64Patcher* g_patcherMicro;
extern UiClientUser* g_playerUi;

extern std::atomic<bool> g_clientHasInitiliazed;

extern pthread_cond_t g_multCond;
extern pthread_mutex_t g_multExclusive;

extern "C" void JNI_OnUnload([[maybe_unused]] JavaVM* vm, [[maybe_unused]] void* reserved)
{
    useriDsp("Unload all used resources");

    if (g_playerUi)
        delete g_playerUi;
    if (g_patcherMicro)
        delete g_patcherMicro;

    if (g_textureDatabase)
        delete g_textureDatabase;
    if (g_clientHasInitiliazed) {}
}

extern "C" JNIEXPORT void JNICALL Java_com_rockstargames_gtasa_GTASA_jvmClientIsReady(
    [[maybe_unused]] JNIEnv* env, jobject gtaClass) 
{
    g_gtaSA = gtaClass;
    useriDsp("jvmClientIsReady() has been called from JVM");

    pthread_mutex_lock(&g_multExclusive);
    pthread_cond_broadcast(&g_multCond);
    pthread_mutex_unlock(&g_multExclusive);

    // This thread should fall to the end, before game itself starts
}

static struct sigaction originSigSegv;

[[noreturn]] void segvSaHandler(int32_t signal, siginfo_t* info, void* ctxPtr)
{
    auto segvContext = reinterpret_cast<ucontext_t*>(ctxPtr);
    const uint64_t faultAddress = segvContext->uc_mcontext.fault_address;

    usereDsp("\n\tSIGSEGV fault in address or page: %#p (access or dereference)\n\nComputer state:", faultAddress);
    usereDsp("\tGTASA base library around: %#p", g_gameAddr);

    uint64_t PC = segvContext->uc_mcontext.pc;
    
    uint64_t pcOffset = (PC & 0xffffff) - (g_gameAddr & 0xffffff);
    if (!(pcOffset & (static_cast<uint64_t>(0xffffffff) << 32))) {
        usereDsp("\tGame space region without base library, at: %#p", pcOffset);
    }

    usereDsp("\t1. Backtrace # pc at: Hex: %#p, Dec: %llu", PC, PC);
    usereDsp("\t2. Backtrace # sp at %#p", segvContext->uc_mcontext.sp);

    // Restoring and calling the original Android sigsegv handler
    sigaction(signal, &originSigSegv, nullptr);

    if (originSigSegv.sa_flags & SA_SIGINFO)
        originSigSegv.sa_sigaction(signal, info, ctxPtr);
    else if (originSigSegv.sa_handler != SIG_DFL && originSigSegv.sa_handler != SIG_IGN)
        originSigSegv.sa_handler(signal);

    raise(SIGSEGV);

    __builtin_unreachable();
}

extern "C" jint JNI_OnLoad(JavaVM* vm, [[maybe_unused]] void* reserved)
{
    useriDsp("GTA:SA CO-OP has loaded, build date: " __DATE__ " " __TIME__);
    
    coutFmt(Info, "Loaded by thread id {} in core {}", std::this_thread::get_id(), sched_getcpu());
    const jint useVersion{JNI_VERSION_1_6};

    JavaVMAttachArgs attachThread{.version = useVersion, .name = "JNI_OnLoad"};
    vm->AttachCurrentThread(&g_gameEnv, &attachThread);

    if (vm->GetEnv(reinterpret_cast<void**>(&g_gameEnv), useVersion) != JNI_OK) {
        usereDsp("Can't get the JNI interface!");
        vm->DetachCurrentThread();
        return JNI_ERR;
    }
    
    std::array<char, 40> gtasaPackage;
    if (getPackageIdentifier(gtasaPackage)) {
        if (gtasaPackage.data() != std::string("com.rockstargames.gtasa"))
            std::terminate();
    }

    // Fetching the base library address of GTASA in memory (precisely where the JVM has loaded the game engine)
    g_gameAddr = getLibrary("libGTASA.so");
    g_audioBackend = getLibrary("libOpenAL64.so");

    static const struct sigaction ourHandler{.sa_flags = SA_SIGINFO,
        .sa_sigaction = segvSaHandler};
    sigaction(SIGSEGV, &ourHandler, &originSigSegv);

    SALOG_ASSERT(g_gameAddr && g_audioBackend, "Can't found a valid address space of GTASA and/or OpenAL, "
        "SAMP is being halted now :[");
    useriDsp("Native libraries base region address found in:\n"
        "1. (GTASA) (%#lx)\n2. (OpenAL64) (%#lx)", g_gameAddr, g_audioBackend);

    // Applying patches and hooking some methods
    applyOnGame();
    pthread_mutex_init(&g_multExclusive, nullptr);
    pthread_cond_init(&g_multCond, nullptr);
    pthread_t clientThread;
    pthread_create(&clientThread, nullptr, enterMainLoop, nullptr);

    return useVersion;
}

