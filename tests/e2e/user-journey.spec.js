import { expect, request as playwrightRequest, test } from '@playwright/test'
import { setTimeout as delay } from 'node:timers/promises'

const testKey = process.env.TEST_AUTH_KEY
const appOrigin = new URL(process.env.PLAYWRIGHT_BASE_URL || 'http://localhost:3000').origin
const videoBase64 = 'GkXfo59ChoEBQveBAULygQRC84EIQoKEd2VibUKHgQJChYECGFOAZwEAAAAAAANPEU2bdLpNu4tTq4QVSalmU6yBoU27i1OrhBZUrmtTrIHWTbuMU6uEElTDZ1OsggEyTbuMU6uEHFO7a1OsggM57AEAAAAAAABZAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAVSalmsCrXsYMPQkBNgIxMYXZmNjMuMS4xMDFXQYxMYXZmNjMuMS4xMDFEiYhAgEAAAAAAABZUrmvXrgEAAAAAAABO14EBc8WI2yIGNR+tgaicgQAitZyDdW5kiIEAhoVWX1ZQOIOBASPjg4QCYloA4JCwgaC6gXiagQJVsIRVuYEBVe6BAOwBAAAAAAAAAgAAElTDZ/pzc59jwIBnyJlFo4dFTkNPREVSRIeMTGF2ZjYzLjEuMTAxc3PVY8CLY8WI2yIGNR+tgahnyKBFo4dFTkNPREVSRIeTTGF2YzYzLjEuMTAxIGxpYnZweGfIoUWjiERVUkFUSU9ORIeTMDA6MDA6MDAuNTIwMDAwMDAwAB9DtnVBgueBAKPFgQAAgFAGAJ0BKqAAeAAARwiFhYiFhIgCAgAGIg8FEJxS0qE4paVCcUtKhOKWlQnFLSoTilpUJxS0qE4paVCbAP7/q1CAo5iBACgAEQIAARAQABgAGFgv9AAIgIEMsACjmIEAUAARAgABEBAAGAAYWC/0AAiAgQywAKOYgQB4ABECAAEQEAAYABhYL/QACICBDLAAo5iBAKAAEQIAARAQABgAGFgv9AAIgIEMsACjmIEAyAARAgABEBAAGAAYWC/0AAiAgQywAKOYgQDwABECAAEQEAAYABhYL/QACICBDLAAo5iBARgAEQIAARAQFGAAYWC/0AAiAgQywACjmIEBQAARAgABEBAAGAAYWC/0AAiAgQywAKOYgQFoABECAAEQEAAYABhYL/QACICBDLAAo5iBAZAAEQIAARAQABgAGFgv9AAIgIEMsACjmIEBuAARAgABEBAAGAAYWC/0AAiAgQywAKOYgQHgABECAAEQEAAYABhYL/QACICBDLAAHFO7a5G7j7OBALeK94EB8YIBsfCBAw=='

test('login restoration, upload, playback, chat, theme, social actions, routes, deletion, and logout', async ({ page, browser }) => {
  test.skip(!testKey, 'Set TEST_AUTH_KEY and enable backend test auth')
  const userId = `browser-${Date.now()}`
  const caption = `Browser journey ${Date.now()}`
  const login = await page.request.post('/api/test/login', {
    headers: { Origin: appOrigin, 'X-Test-Auth-Key': testKey },
    data: { id: userId, name: 'Browser Student' },
  })
  expect(login.ok()).toBeTruthy()

  await page.goto('/settings')
  await page.getByLabel('Display name').fill('Browser Student Plus')
  await page.getByLabel('Bio').fill('Testing upgraded profiles')
  await page.route('**/api/me/profile', async (route) => {
    if (route.request().method() === 'PUT') await delay(350)
    await route.continue()
  }, { times: 1 })
  await page.getByRole('button', { name: 'Save profile' }).click()
  await expect(page.getByRole('progressbar', { name: 'Saving profile' })).toBeVisible()
  await expect(page.getByRole('button', { name: 'Saving changes...' })).toBeDisabled()
  await expect(page.getByRole('status')).toContainText('Profile saved')
  await page.locator('input[accept="image/jpeg,image/png,image/webp"]').setInputFiles({
    name: 'avatar.png',
    mimeType: 'image/png',
    buffer: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Zl1sAAAAASUVORK5CYII=', 'base64'),
  })
  await expect(page.getByRole('status')).toContainText('Profile photo updated')

  await page.goto('/upload')
  await expect(page.getByText('Upload Video', { exact: true }).first()).toBeVisible()
  await page.locator('input[type=file][name="video"]').setInputFiles({
    name: 'browser-demo.webm',
    mimeType: 'video/webm',
    buffer: Buffer.from(videoBase64, 'base64'),
  })
  await page.getByLabel('Caption', { exact: true }).fill(caption)
  await page.getByLabel('Choose A Category').selectOption('Gaming')
  await page.getByLabel('Captions (optional)').setInputFiles({
    name: 'captions.vtt',
    mimeType: 'text/vtt',
    buffer: Buffer.from('WEBVTT\n\n00:00:00.000 --> 00:00:00.500\nSchool project caption\n'),
  })
  await page.getByRole('button', { name: 'Post' }).click()
  await expect(page).toHaveURL(/\/detail\//)
  const detailUrl = page.url()
  await expect(page.getByText(caption, { exact: true })).toBeVisible()

  const video = page.getByLabel(`Video: ${caption}`)
  await expect(video.locator('track[kind="captions"]')).toHaveCount(1)
  await expect.poll(() => video.evaluate((element) => element.readyState)).toBeGreaterThanOrEqual(1)
  const seeked = await video.evaluate(async (element) => {
    element.currentTime = Math.min(0.4, element.duration * 0.75)
    await new Promise((resolve) => element.addEventListener('seeked', resolve, { once: true }))
    return { currentTime: element.currentTime, duration: element.duration }
  })
  expect(seeked.duration).toBeGreaterThan(0)
  expect(seeked.currentTime).toBeGreaterThan(0)
  await page.getByRole('button', { name: 'Play video' }).click()
  await page.getByRole('button', { name: 'Save video' }).click()
  await expect(page.getByRole('button', { name: 'Remove from saved videos' })).toBeVisible()
  await page.evaluate(() => Object.defineProperty(navigator, 'share', { configurable: true, value: () => Promise.resolve() }))
  await page.getByRole('button', { name: 'Share video' }).click()
  await expect(page.getByText(/1 shares/)).toBeVisible()

  await page.goto('/library')
  await expect(page.getByText(caption, { exact: true })).toBeVisible()
  await page.getByRole('tab', { name: 'Watch history' }).click()
  await expect(page.getByText(caption, { exact: true })).toBeVisible()
  await page.goto(detailUrl)

  await page.reload()
  await expect(page.getByText(caption, { exact: true })).toBeVisible()
  await expect(page.getByRole('link', { name: 'Your profile' })).toBeVisible()

  await page.getByRole('button', { name: 'Like video' }).click()
  await expect(page.getByRole('button', { name: 'Unlike video' })).toHaveAttribute('aria-pressed', 'true')
  await page.getByLabel('Add comment').fill('Browser comment works')
  await page.getByRole('button', { name: 'Comment', exact: true }).click()
  await expect(page.getByText('Browser comment works')).toBeVisible()

  await page.goto('/')
  await page.getByLabel('Search accounts and videos').fill(caption)
  await page.getByRole('button', { name: 'Search', exact: true }).click()
  await expect(page).toHaveURL(/\/search\//)
  await expect(page.getByText(caption, { exact: true })).toBeVisible()

  await page.goto(`/profile/${encodeURIComponent(`test:${userId}`)}`)
  await expect(page.locator('main').getByText('Browser Student Plus', { exact: true }).first()).toBeVisible()
  await expect(page.getByText('Testing upgraded profiles', { exact: true })).toBeVisible()
  await page.getByRole('tab', { name: 'Liked' }).click()
  await expect(page.getByText(caption, { exact: true })).toBeVisible()

  const peerId = `browser-peer-${Date.now()}`
  const peerContext = await browser.newContext({ baseURL: new URL(page.url()).origin })
  const peerLogin = await peerContext.request.post('/api/test/login', {
    headers: { Origin: appOrigin, 'X-Test-Auth-Key': testKey },
    data: { id: peerId, name: 'Chat Partner' },
  })
  expect(peerLogin.ok()).toBeTruthy()
  const peerPage = await peerContext.newPage()

  await page.goto(`/profile/${encodeURIComponent(`test:${peerId}`)}`)
  await page.getByRole('button', { name: 'Follow', exact: true }).click()
  await expect(page.getByRole('button', { name: 'Following', exact: true })).toBeVisible()
  await page.getByRole('button', { name: 'Report account' }).click()
  await page.getByLabel('Reason').selectOption('spam')
  await page.getByLabel('Additional details (optional)').fill('Automated browser test report')
  await page.getByRole('button', { name: 'Submit report' }).click()
  await expect(page.getByText(/Report submitted/)).toBeVisible()
  await page.getByRole('button', { name: 'Cancel' }).click()

  await peerPage.goto('/notifications')
  await expect(peerPage.getByText('started following you')).toBeVisible()

  await peerPage.goto(detailUrl)
  await peerPage.evaluate(() => Object.defineProperty(navigator, 'share', { configurable: true, value: () => Promise.resolve() }))
  await peerPage.getByRole('button', { name: 'Share video' }).click()
  await expect(page.getByText('Someone shared your video.', { exact: true })).toBeVisible()
  await peerPage.getByLabel('Add comment').fill('Peer comment for interactions')
  await peerPage.getByRole('button', { name: 'Comment', exact: true }).click()
  await expect(peerPage.getByText('Peer comment for interactions')).toBeVisible()
  const peerOwnComment = peerPage.locator('.comment-item').filter({ hasText: 'Peer comment for interactions' })
  await peerOwnComment.getByRole('button', { name: 'Edit', exact: true }).click()
  await peerOwnComment.getByLabel('Edit comment').fill('Peer comment edited')
  await peerOwnComment.getByRole('button', { name: 'Save', exact: true }).click()
  await expect(peerPage.locator('.comment-item').filter({ hasText: 'Peer comment edited' }).getByText('Edited', { exact: true })).toBeVisible()

  await page.goto(detailUrl)
  const peerComment = page.locator('.comment-item').filter({ hasText: 'Peer comment edited' })
  await peerComment.getByRole('button', { name: 'Like comment' }).click()
  await expect(peerComment.getByRole('button', { name: 'Unlike comment' })).toBeVisible()
  await peerComment.getByRole('button', { name: 'Pin comment' }).click()
  await expect(peerComment.getByText('Pinned by creator')).toBeVisible()
  await peerComment.getByRole('button', { name: 'Reply', exact: true }).click()
  await peerComment.getByLabel('Reply to Chat Partner').fill('Owner reply works')
  await peerComment.getByRole('button', { name: 'Reply', exact: true }).last().click()
  await expect(peerComment.getByText('Owner reply works')).toBeVisible()

  await peerPage.goto('/notifications')
  await expect(peerPage.getByText('shared your video')).toHaveCount(0)
  await expect(peerPage.getByText('liked your comment')).toBeVisible()
  await expect(peerPage.getByText('replied to your comment')).toBeVisible()
  await expect(peerPage.getByText('pinned your comment')).toBeVisible()

  await page.goto('/notifications')
  await expect(page.getByText('shared your video')).toBeVisible()

  await page.goto(`/messages/${encodeURIComponent(`test:${peerId}`)}`)
  await expect(page.getByText('Chat Partner', { exact: true }).first()).toBeVisible()
  await page.getByRole('textbox', { name: 'Message', exact: true }).fill('Hello from the browser test')
  await page.getByRole('button', { name: 'Send message' }).click()
  await expect(page.locator('.chat-messages').getByText('Hello from the browser test', { exact: true })).toBeVisible()

  await peerPage.goto(`/messages/${encodeURIComponent(`test:${userId}`)}`)
  await expect(peerPage.locator('.chat-messages').getByText('Hello from the browser test', { exact: true })).toBeVisible()
  const receivedBubble = peerPage.locator('.message-bubble').filter({ hasText: 'Hello from the browser test' })
  await receivedBubble.getByRole('button', { name: 'Reply to message' }).click()
  await expect(peerPage.getByText('Replying to: Hello from the browser test')).toBeVisible()
  await receivedBubble.getByRole('button', { name: '❤️' }).click()
  await peerPage.getByRole('textbox', { name: 'Message', exact: true }).fill('The reply works too')
  await expect(page.getByText('Typing…')).toBeVisible({ timeout: 10_000 })
  await peerPage.getByRole('button', { name: 'Send message' }).click()
  await expect(page.locator('.chat-messages').getByText('The reply works too', { exact: true })).toBeVisible({ timeout: 10_000 })
  await expect(page.getByText('Replying to Hello from the browser test')).toBeVisible()
  await expect(page.locator('.message-reactions').getByText('❤️')).toBeVisible({ timeout: 10_000 })

  const peerSeedContext = await playwrightRequest.newContext({ baseURL: appOrigin })
  const peerSeedLogin = await peerSeedContext.post('/api/test/login', {
    headers: { Origin: appOrigin, 'X-Test-Auth-Key': testKey },
    data: { id: peerId, name: 'Chat Partner' },
  })
  expect(peerSeedLogin.ok()).toBeTruthy()
  const peerSeedSession = await peerSeedLogin.json()
  const peerSeedCsrf = peerSeedSession.data.csrfToken
  for (let index = 0; index < 18; index += 1) {
    const response = await peerSeedContext.post(`/api/chat/${encodeURIComponent(`test:${userId}`)}/messages`, {
      headers: { Origin: appOrigin, 'X-CSRF-Token': peerSeedCsrf },
      data: { message: `Scroll test message ${index + 1}: this keeps older messages readable while new chat activity arrives.` },
    })
    expect(response.ok(), `scroll seed ${index + 1} failed (${response.status()}): ${await response.text()}`).toBeTruthy()
  }
  await expect(page.locator('.chat-messages').getByText('Scroll test message 18:', { exact: false })).toBeVisible({ timeout: 10_000 })
  const scrollable = await page.locator('.chat-messages').evaluate((element) => element.scrollHeight > element.clientHeight)
  expect(scrollable).toBeTruthy()
  await page.locator('.chat-messages').evaluate((element) => { element.scrollTop = 0 })
  await expect(page.getByRole('button', { name: /Jump to latest|new messages/ })).toBeVisible()
  await page.waitForTimeout(4_500)
  const preservedScrollTop = await page.locator('.chat-messages').evaluate((element) => element.scrollTop)
  expect(preservedScrollTop).toBeLessThan(20)
  const incomingAfterScroll = await peerSeedContext.post(`/api/chat/${encodeURIComponent(`test:${userId}`)}/messages`, {
    headers: { Origin: appOrigin, 'X-CSRF-Token': peerSeedCsrf },
    data: { message: 'Newest message while reading older chat' },
  })
  expect(incomingAfterScroll.ok()).toBeTruthy()
  await expect(page.getByRole('button', { name: /new message/ })).toBeVisible({ timeout: 10_000 })
  await page.getByRole('button', { name: /new message/ }).click()
  await expect(page.locator('.chat-messages').getByText('Newest message while reading older chat', { exact: true })).toBeVisible()
  await peerSeedContext.dispose()

  await peerPage.locator('input[type=file][accept="image/jpeg,image/png,image/webp"]').setInputFiles({
    name: 'chat.png',
    mimeType: 'image/png',
    buffer: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Zl1sAAAAASUVORK5CYII=', 'base64'),
  })
  await peerPage.getByRole('button', { name: 'Send message' }).click()
  await expect(page.getByRole('img', { name: 'chat.png' })).toBeVisible({ timeout: 10_000 })
  await page.getByRole('button', { name: 'Mute conversation' }).click()
  await expect(page.getByRole('button', { name: 'Unmute conversation' })).toBeVisible()
  await page.getByRole('button', { name: 'Unmute conversation' }).click()

  await page.getByRole('button', { name: 'Switch to dark mode' }).click()
  await expect(page.locator('html')).toHaveClass(/dark/)
  const suggestedAccount = page.locator('.suggested-account').first()
  await suggestedAccount.hover()
  await expect.poll(() => suggestedAccount.evaluate((element) => window.getComputedStyle(element).backgroundColor))
    .not.toBe('rgb(241, 241, 242)')
  await page.reload()
  await expect(page.locator('html')).toHaveClass(/dark/)
  await page.getByRole('button', { name: 'Switch to light mode' }).click()
  await peerContext.close()

  await page.goto('/definitely-not-a-route')
  await expect(page.getByText('That page does not exist.')).toBeVisible()
  const context = page.context()
  await page.close()
  const deletePage = await context.newPage()
  await deletePage.goto(detailUrl)
  deletePage.once('dialog', (dialog) => dialog.accept())
  await deletePage.getByRole('button', { name: 'Delete video' }).click()
  await expect(deletePage).toHaveURL(`${appOrigin}/`)

  await deletePage.getByRole('button', { name: 'Log out' }).click()
  await expect(deletePage.getByRole('link', { name: 'Your profile' })).toHaveCount(0)
  await deletePage.reload()
  await expect(deletePage.getByRole('link', { name: 'Your profile' })).toHaveCount(0)
})

test('empty states and responsive navigation remain useful', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto(`/search/no-match-${Date.now()}`)
  await expect(page.getByText(/No Video Results Found/)).toBeVisible()
  await page.getByRole('tab', { name: 'Accounts' }).click()
  await expect(page.getByText(/No Account Results Found/)).toBeVisible()
  await page.getByRole('button', { name: 'Toggle sidebar' }).click()
  await page.getByRole('button', { name: 'Toggle sidebar' }).click()
  await expect(page.getByText('For You')).toBeHidden()
})
