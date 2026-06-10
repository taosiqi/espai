export function getTelegramLaunchParams() {
  const searchParams = new URLSearchParams(location.search)
  const hashParams = new URLSearchParams(location.hash.replace(/^#/, ''))
  const keys = [
    'tgWebAppData',
    'tgWebAppVersion',
    'tgWebAppPlatform',
    'tgWebAppThemeParams',
    'tgWebAppStartParam'
  ]

  return keys.reduce((params, key) => {
    const value = searchParams.get(key) || hashParams.get(key)
    if (value) params[key] = value
    return params
  }, {})
}

export function isTelegramMiniAppLaunch() {
  const params = getTelegramLaunchParams()

  return Boolean(
    window.Telegram?.WebApp?.initData ||
      params.tgWebAppData ||
      (params.tgWebAppVersion && params.tgWebAppPlatform)
  )
}

export function loadTelegramSdk() {
  if (window.Telegram?.WebApp) {
    return Promise.resolve(window.Telegram.WebApp)
  }

  return new Promise((resolve, reject) => {
    const existing = document.querySelector('script[data-telegram-web-app-sdk]')
    if (existing) {
      existing.addEventListener('load', () => resolve(window.Telegram?.WebApp), { once: true })
      existing.addEventListener('error', reject, { once: true })
      return
    }

    const script = document.createElement('script')
    script.src = 'https://telegram.org/js/telegram-web-app.js'
    script.async = true
    script.dataset.telegramWebAppSdk = 'true'
    script.onload = () => resolve(window.Telegram?.WebApp)
    script.onerror = () => reject(new Error('Telegram SDK 加载失败'))
    document.head.appendChild(script)
  })
}
