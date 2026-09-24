const DATABASE = 'tiktik-local-drafts'
const STORE = 'uploads'
const KEY = 'current'

const openDatabase = () => new Promise((resolve, reject) => {
  if (!('indexedDB' in window)) { reject(new Error('Draft storage is unavailable.')); return }
  const request = window.indexedDB.open(DATABASE, 1)
  request.onupgradeneeded = () => request.result.createObjectStore(STORE)
  request.onsuccess = () => resolve(request.result)
  request.onerror = () => reject(request.error)
})

const transaction = async (mode, action) => {
  const database = await openDatabase()
  return new Promise((resolve, reject) => {
    const tx = database.transaction(STORE, mode)
    const request = action(tx.objectStore(STORE))
    request.onsuccess = () => resolve(request.result)
    request.onerror = () => reject(request.error)
    tx.oncomplete = () => database.close()
  })
}

export const saveUploadDraft = (draft) => transaction('readwrite', (store) => store.put(draft, KEY))
export const loadUploadDraft = () => transaction('readonly', (store) => store.get(KEY))
export const clearUploadDraft = () => transaction('readwrite', (store) => store.delete(KEY))
