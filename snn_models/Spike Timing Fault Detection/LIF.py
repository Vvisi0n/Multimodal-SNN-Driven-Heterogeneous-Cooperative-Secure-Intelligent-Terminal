import tensorflow as tf


def spike_with_sigmoid_grad(x, alpha=4.0):
    """Forward: Heaviside spike. Backward: sigmoid surrogate gradient."""
    alpha = tf.cast(alpha, x.dtype)

    @tf.custom_gradient
    def _spike(z):
        y = tf.cast(z >= 0.0, z.dtype)

        def grad(dy):
            s = tf.sigmoid(alpha * z)
            return dy * alpha * s * (1.0 - s)

        return y, grad

    return _spike(x)


class LIFCell(tf.keras.layers.Layer):
    """SpikingJelly-like LIF RNN cell.

    inputs/state shape per step: (batch, ...)
    output spike shape:          (batch, ...)
    """

    def __init__(
        self,
        size,
        tau=2.0,
        decay_input=True,
        v_threshold=1.0,
        v_reset=0.0,
        surrogate_alpha=4.0,
        detach_reset=False,
        **kwargs,
    ):
        super().__init__(**kwargs)
        if tau <= 1.0:
            raise ValueError("tau should be > 1.0, same as SpikingJelly LIFNode.")

        self.size = tuple(tf.TensorShape(size).as_list())
        self.state_size = self.size
        self.output_size = self.size

        self.tau = float(tau)
        self.decay_input = bool(decay_input)
        self.v_threshold = float(v_threshold)
        self.v_reset = None if v_reset is None else float(v_reset)
        self.surrogate_alpha = float(surrogate_alpha)
        self.detach_reset = bool(detach_reset)

    def get_initial_state(self, inputs=None, batch_size=None, dtype=None):
        dtype = dtype or tf.keras.backend.floatx()
        v0 = 0.0 if self.v_reset is None else self.v_reset
        return tf.fill(
            [batch_size] + list(self.size),
            tf.cast(v0, dtype),
        )

    def call(self, inputs, states, training=None):
        v = states[0]

        if self.decay_input:
            if self.v_reset is None or self.v_reset == 0.0:
                v = v + (inputs - v) / self.tau
            else:
                v = v + (inputs - (v - self.v_reset)) / self.tau
        else:
            if self.v_reset is None or self.v_reset == 0.0:
                v = v * (1.0 - 1.0 / self.tau) + inputs
            else:
                v = v - (v - self.v_reset) / self.tau + inputs

        spike = spike_with_sigmoid_grad(
            v - self.v_threshold,
            alpha=self.surrogate_alpha,
        )

        spike_for_reset = tf.stop_gradient(spike) if self.detach_reset else spike

        if self.v_reset is None:
            # soft reset: v = v - spike * v_threshold
            v = v - spike_for_reset * self.v_threshold
        else:
            # hard reset: v = v_reset if spike else v
            v = spike_for_reset * self.v_reset + (1.0 - spike_for_reset) * v

        return spike, [v]


class LIF(tf.keras.layers.Layer):
    """Keras layer wrapper. Input shape: (batch, time, ...)."""

    def __init__(
        self,
        return_sequences=True,
        return_state=False,
        unroll=True,
        **lif_kwargs,
    ):
        super().__init__()
        self.return_sequences = return_sequences
        self.return_state = return_state
        self.unroll = unroll
        self.lif_kwargs = lif_kwargs
        self.rnn = None

    def build(self, input_shape):
        cell = LIFCell(size=input_shape[2:], **self.lif_kwargs)
        self.rnn = tf.keras.layers.RNN(
            cell,
            return_sequences=self.return_sequences,
            return_state=self.return_state,
            unroll=self.unroll,
        )
        self.rnn.build(input_shape)
        super().build(input_shape)

    def call(self, inputs, training=None, initial_state=None):
        return self.rnn(inputs, training=training, initial_state=initial_state)
