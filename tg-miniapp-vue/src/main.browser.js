import { createApp, computed, onMounted, ref } from 'https://unpkg.com/vue@3/dist/vue.esm-browser.prod.js'
import { EXTERNAL_URL, INTERNAL_URL, SAMPLE_BASE64, TG_SHARE_TEXT } from './constants.js'
import { getTelegramLaunchParams, isTelegramMiniAppLaunch, loadTelegramSdk } from './telegram.js'

createApp({
  setup() {
    let webApp = null
    const logs = ref([])
    const launchParams = ref(getTelegramLaunchParams())
    const launchUrl = ref(window.location.href)
    const isTelegram = ref(isTelegramMiniAppLaunch())
    const sdkLoaded = ref(false)
    const initData = ref('')
    const initDataUnsafe = ref(null)
    const viewport = ref({
      width: window.innerWidth,
      height: window.innerHeight
    })

    const statusText = computed(() => {
      if (!isTelegram.value) return '普通浏览器预览'
      if (!sdkLoaded.value) return 'Mini App 环境，SDK 加载中'
      return `Telegram Mini App v${webApp?.version || 'unknown'}`
    })

    function addLog(message) {
      const time = new Date().toLocaleTimeString('zh-CN', { hour12: false })
      logs.value = [{ time, message }, ...logs.value].slice(0, 6)
    }

    function printLaunchUrl() {
      launchUrl.value = window.location.href
      launchParams.value = getTelegramLaunchParams()
      console.log('[MiniApp URL]', launchUrl.value)
      console.log('[MiniApp launch params]', launchParams.value)
      addLog(`启动 URL：${launchUrl.value}`)
    }

    function supports(methodName) {
      return typeof webApp?.[methodName] === 'function'
    }

    async function setupTelegramSdk() {
      if (!isTelegram.value) {
        addLog('非 Mini App 环境，跳过 Telegram SDK')
        return false
      }

      try {
        webApp = await loadTelegramSdk()
        sdkLoaded.value = Boolean(webApp)
        initData.value = webApp?.initData || ''
        initDataUnsafe.value = webApp?.initDataUnsafe || null
      } catch (error) {
        addLog(error.message)
        return false
      }

      if (!webApp) {
        addLog('Mini App 参数存在，但未获得 Telegram WebApp 对象')
        return false
      }

      return true
    }

    async function readyMiniApp() {
      const available = await setupTelegramSdk()
      if (!available) {
        return
      }

      webApp.ready()
      webApp.expand()
      webApp.disableVerticalSwipes?.()
      webApp.setHeaderColor?.('#111827')
      webApp.setBackgroundColor?.('#0f172a')
      addLog('已初始化：ready、expand')
    }

    function isFullscreenNow() {
      if (typeof webApp?.isFullscreen === 'boolean') {
        return webApp.isFullscreen
      }
    
      return Boolean(document.fullscreenElement)
    }

    function requestFullscreenOnly() {
      try {
        if (isFullscreenNow()) {
          addLog('当前已经是全屏，无需重复请求')
          return
        }
    
        if (supports('requestFullscreen')) {
          webApp.requestFullscreen()
          addLog('已请求 Telegram 全屏，请手动旋转手机到横屏')
          return
        }
    
        if (document.documentElement.requestFullscreen) {
          document.documentElement.requestFullscreen()
          addLog('已请求浏览器全屏，请手动旋转手机到横屏')
          return
        }
    
        addLog('当前环境不支持全屏')
      } catch (error) {
        addLog(`请求全屏失败：${error.message}`)
      }
    }

    function openInternalBrowser() {
      window.open(INTERNAL_URL, '_blank', 'noopener,noreferrer')
      addLog('已打开内部 WebView 链接')
    }

    function openExternalBrowser() {
      try {
        if (supports('openLink')) {
          webApp.openLink(EXTERNAL_URL)
          addLog('已请求外部浏览器打开')
          return
        }
        window.open(EXTERNAL_URL, '_blank', 'noopener,noreferrer')
        addLog('已打开外部链接')
      } catch (error) {
        addLog(`外部浏览器失败：${error.message}`)
      }
    }
    function openTelegramShare() {
      const shareUrl = `https://t.me/share/url?url=${encodeURIComponent(location.href)}&text=${encodeURIComponent(TG_SHARE_TEXT)}`
      try {
        if (supports('openTelegramLink')) {
          webApp.openTelegramLink(shareUrl)
          addLog('已打开 TG 分享链接')
          return
        }
        window.open(shareUrl, '_blank', 'noopener,noreferrer')
        addLog('已打开 TG 分享链接')
      } catch (error) {
        addLog(`TG 分享链接失败：${error.message}`)
      }
    }
    async function copyInitData() {
      await navigator.clipboard?.writeText(initData.value || '')
      addLog(initData.value ? '已复制 initData' : 'initData 为空')
    }
    function updateViewport() {
      viewport.value = {
        width: window.innerWidth,
        height: window.innerHeight
      }
    }

    onMounted(async () => {
      printLaunchUrl()
      await readyMiniApp()
      window.addEventListener('resize', updateViewport)
    })

    return {
      SAMPLE_BASE64,
      logs,
      launchParams,
      launchUrl,
      openExternalBrowser,
      openInternalBrowser,
      initData,
      initDataUnsafe,
      openTelegramShare,
      copyInitData,
      requestFullscreenOnly,
      statusText,
      supports,
      viewport
    }
  },
  template: `
    <main class="shell">
      <section class="panel">
        <div class="topbar">
          <div>
            <p class="eyebrow">Telegram Mini App</p>
            <h1>Vue 控制台</h1>
          </div>
          <span class="status">{{ statusText }}</span>
        </div>

        <div class="meter">
          <div>
            <span>Viewport</span>
            <strong>{{ viewport.width }} x {{ viewport.height }}</strong>
          </div>
          <div>
            <span>Fullscreen</span>
            <strong>{{ supports('requestFullscreen') ? 'TG 可用' : 'Fallback' }}</strong>
          </div>
          <div>
            <span>SDK</span>
            <strong>{{ statusText }}</strong>
          </div>
        </div>

        <div class="actions" aria-label="Telegram Mini App actions">
          <button type="button" @click="requestFullscreenOnly">请求全屏</button>
          <button type="button" @click="openInternalBrowser">打开内部浏览器</button>
          <button type="button" @click="openExternalBrowser">打开外部浏览器</button>
          <button type="button" @click="openTelegramShare">TG 分享链接</button>
          <button type="button" @click="copyInitData">复制 initData</button>
        </div>
      </section>

      <section class="preview">
        <img :src="SAMPLE_BASE64" alt="base64 preview" />
        <div class="initdata">
          <span>launchUrl</span>
          <code>{{ launchUrl }}</code>
          <span>launchParams</span>
          <code>{{ JSON.stringify(launchParams, null, 2) }}</code>
          <span>initData</span>
          <code>{{ initData || '暂无 initData' }}</code>
          <span>initDataUnsafe</span>
          <code>{{ initDataUnsafe ? JSON.stringify(initDataUnsafe, null, 2) : '暂无 initDataUnsafe' }}</code>
        </div>
        <div class="log">
          <p v-if="logs.length === 0">等待操作...</p>
          <p v-for="item in logs" :key="item.time + item.message">
            <span>{{ item.time }}</span>{{ item.message }}
          </p>
        </div>
      </section>
    </main>
  `
}).mount('#app')
