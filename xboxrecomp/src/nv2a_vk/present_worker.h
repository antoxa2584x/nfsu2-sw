/* Private to nv2a_vk.c. One presentation job, no extra GPU frames in flight.
 * NVK's Switch WSI waits for the GPU inside QueuePresent. Record the next
 * frame during that wait, but join before acquire or any other queue access. */
static struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    int started, pending, done, stop;
    VkSwapchainKHR swapchain;
    VkSemaphore semaphore;
    uint32_t index, width, height;
    VkResult result;
    uint64_t elapsed, completed;
} s_present = { .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };

static void present_account(VkResult result, uint64_t elapsed, uint64_t completed)
{
    (void)elapsed;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        vk_require(result, "swapchain present (restart required)");
    fps_counter_present(&s_fps, completed);
}

static void *present_run(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&s_present.mutex);
    for (;;) {
        while ((!s_present.pending || s_present.done) && !s_present.stop)
            pthread_cond_wait(&s_present.cond, &s_present.mutex);
        if (s_present.stop) break;
        /* These handles and the index are immutable until the owner joins. */
        VkSwapchainKHR swapchain = s_present.swapchain;
        VkSemaphore semaphore = s_present.semaphore;
        uint32_t index = s_present.index;
        pthread_mutex_unlock(&s_present.mutex);
        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = pi.swapchainCount = 1;
        pi.pWaitSemaphores = &semaphore;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &index;
        uint64_t started = hz_now();
        VkResult result = vkQueuePresentKHR(s_queue, &pi);
        uint64_t completed = hz_now();
        pthread_mutex_lock(&s_present.mutex);
        s_present.result = result;
        s_present.completed = completed;
        s_present.elapsed = completed - started;
        s_present.done = 1;
        pthread_cond_broadcast(&s_present.cond);
    }
    pthread_mutex_unlock(&s_present.mutex);
    return NULL;
}

/* Only the renderer owner updates diagnostics and handles errors. The worker
 * never aborts or invokes cleanup, avoiding a self-join on fatal presentation. */
static void present_drain(void)
{
    if (!s_present.started) return;
    pthread_mutex_lock(&s_present.mutex);
    if (!s_present.pending) {
        pthread_mutex_unlock(&s_present.mutex);
        return;
    }
    while (!s_present.done) pthread_cond_wait(&s_present.cond, &s_present.mutex);
    VkResult result = s_present.result;
    uint64_t elapsed = s_present.elapsed, completed = s_present.completed;
    s_present.pending = s_present.done = 0;
    pthread_mutex_unlock(&s_present.mutex);
    present_account(result, elapsed, completed);
    if (nv2a_host_frame_presented) nv2a_host_frame_presented(s_present.width, s_present.height);
}

static void present_stop(void)
{
    if (!s_present.started) return;
    /* Shutdown still joins the driver. Do not abort again during termination. */
    pthread_mutex_lock(&s_present.mutex);
    while (s_present.pending && !s_present.done)
        pthread_cond_wait(&s_present.cond, &s_present.mutex);
    s_present.stop = 1;
    pthread_cond_broadcast(&s_present.cond);
    pthread_mutex_unlock(&s_present.mutex);
    pthread_join(s_present.thread, NULL);
    s_present.started = 0;
}

static void present_start(void)
{
    const char *e = getenv("RECOMP_VK_ASYNC_PRESENT");
    if (s_present.started || s_headless || !e || *e != '1') return;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* No guest stack or shader compiler here; bound the extra physical heap. */
    pthread_attr_setstacksize(&attr, 512u << 10);
    int err = pthread_create(&s_present.thread, &attr, present_run, NULL);
    pthread_attr_destroy(&attr);
    if (err) {
        LOGE("async present unavailable (%d); using synchronous output\n", err);
        return;
    }
    s_present.started = 1;
    atexit(present_stop);
    LOGE("async present on: one job, acquire/submit serialized, 512 KB stack\n");
}

static void present_image(uint32_t index, uint32_t width, uint32_t height)
{
    if (s_present.started) {
        pthread_mutex_lock(&s_present.mutex);
        /* The previous job was joined before acquire/submit. */
        s_present.swapchain = s_swapchain;
        s_present.semaphore = s_sc_rendered[index];
        s_present.index = index;
        s_present.width = width; s_present.height = height;
        s_present.pending = 1;
        s_present.done = 0;
        pthread_cond_broadcast(&s_present.cond);
        pthread_mutex_unlock(&s_present.mutex);
    } else {
        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = pi.swapchainCount = 1;
        pi.pWaitSemaphores = &s_sc_rendered[index];
        pi.pSwapchains = &s_swapchain;
        pi.pImageIndices = &index;
        uint64_t started = hz_now();
        VkResult result = vkQueuePresentKHR(s_queue, &pi);
        uint64_t completed = hz_now();
        present_account(result, completed - started, completed);
        if (nv2a_host_frame_presented) nv2a_host_frame_presented(width, height);
    }
}
