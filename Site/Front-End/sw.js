self.addEventListener("push", event => {
  let data = {};
  try { data = event.data ? event.data.json() : {}; } catch {}
  const title = data.title || "Morok";
  const options = {
    body: data.body || "Você tem uma nova atualização.",
    icon: "./assets/morok-icon.png",
    badge: "./assets/morok-badge.png",
    data: { url: data.url || "/" },
    tag: "morok-update",
    renotify: true
  };
  event.waitUntil(self.registration.showNotification(title, options));
});
self.addEventListener("notificationclick", event => {
  event.notification.close();
  const target = new URL(event.notification.data?.url || "/", self.location.origin).href;
  event.waitUntil(clients.matchAll({ type: "window", includeUncontrolled: true }).then(windows => {
    for (const client of windows) {
      if (client.url.startsWith(self.location.origin) && "focus" in client) {
        client.navigate(target);
        return client.focus();
      }
    }
    return clients.openWindow(target);
  }));
});
self.addEventListener("install", event => self.skipWaiting());
self.addEventListener("activate", event => event.waitUntil(clients.claim()));
