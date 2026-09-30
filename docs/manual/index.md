# Switchboard manual

<p class="lead">Switchboard is an e-ink smart-home remote, built on the Xteink X4 Pro, plus the small server that sets it up. The remote drives your lights, blinds, music, TV and heating through Home Assistant. The server can also run colour e-ink wall displays, called viewports.</p>

<div class="shots">
  <figure class="photo"><img src="../images/standby.jpg" alt="The remote on its standby page"><figcaption>A remote on the wall</figcaption></figure>
  <figure><div class="remote"><img src="images/remote/lighting.png" alt="The Lighting page"></div><figcaption>Lighting, as the remote draws it</figcaption></figure>
  <figure><div class="remote"><img src="images/remote/music.png" alt="The Music page"></div><figcaption>Music</figcaption></figure>
</div>

## How it fits together

- **The remote** is dumb hardware. It knows nothing about your house until the server tells it, and it only draws what it's sent.
- **Switchboard Server** runs on your network in a Docker container. Every room's setup lives there: which lights, blinds and players it has, and which pages its remotes show. The server talks to Home Assistant for the devices and prepares everything they draw.
- **Viewports** are colour e-ink wall displays, such as a kitchen panel or a meeting-room sign. Their whole UI is also built on the server.

<figure class="shot"><img src="images/admin/home.png" alt="The server's Home page"><figcaption>The server's Home page: every device, its battery and the days it has left, its Wi-Fi and firmware, and anything that needs attention.</figcaption></figure>

## Where to go next

- **New here?** [Getting started](getting-started.md) takes you from nothing to a working remote.
- **No hardware yet?** [Try the demo](demo.md) runs the server with a pretend Home Assistant and a whole house of devices.
- **Using a remote?** Start with its [screens](remote/screens.md).
- **Setting things up?** Start with the server's [Home page](server/home.md) and [remote layouts](server/remote-layouts.md).

The screenshots in this manual come from the real code: the remote's screens are drawn by its own firmware, and the admin pages come from the server, both running against the [demo](demo.md). See [Screenshots](screenshots.md) for how to make them again.
