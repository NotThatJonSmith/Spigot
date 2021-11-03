# Spigot
A service thread and ring buffer geared for simplicity and low latency

Let's address the obvious hiccup: why use a spinlock here? Isn't that inefficient?

This thing is obsessed with consumer latency. If you're offloading too much work onto the producer thread and underflowing the buffer on the consumer end, you're hosed anyway, so spinning on consume isn't a problem and it's better to not have any locking overhead there.

Spinning on the producer side isn't a problem because this project isn't meant for things that run on batteries ;)
