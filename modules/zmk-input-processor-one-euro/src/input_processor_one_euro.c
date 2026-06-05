/*
 * DRAFT — 1-Euro adaptive low-pass smoothing input processor for ZMK (T1-E).
 *
 * STATUS: NOT COMPILED / NOT FLASHED. This is a ready-to-iterate starting point.
 * Before trusting it: build it, bump stacks (CONFIG_MAIN_STACK_SIZE=4096), enable
 * CONFIG_FPU=y (this draft uses float for readability; the master plan calls for a
 * Q16.16 fixed-point port — do that if RAM/латency demands), and SOAK it. A custom
 * processor that overflows the input thread will brick the board until reflash
 * (RECOVERY kit in firmware/RECOVERY/).
 *
 * What it does: smooths the relative-delta stream so low-speed jitter/shimmer is
 * removed while fast throws keep <1-sample lag. Per-axis (REL_X/REL_Y) state.
 * Time delta (Te) is measured from k_uptime — note that over BLE the central sees
 * delivery in bursts (PREF_LATENCY coalescing), so Te is burst cadence, not true
 * sensor timing; tune accordingly (this is the documented limitation from the plan).
 *
 * Pipeline order (per plan): smooth (this) -> acceleration -> scaler.
 *   input-processors = <&one_euro &pointer_accel ...>;
 *
 * API mirrors badjeff/oleksandrmaslov ZMK input processors (drivers/input_processor.h,
 * struct zmk_input_processor_driver_api { .handle_event }, DT_INST_FOREACH_STATUS_OKAY).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <math.h>

#define DT_DRV_COMPAT zmk_input_processor_one_euro

#ifndef CONFIG_ZMK_INPUT_PROCESSOR_INIT_PRIORITY
#define CONFIG_ZMK_INPUT_PROCESSOR_INIT_PRIORITY 50
#endif

#define ONE_EURO_MAX_CODES 2

struct one_euro_config {
	uint8_t input_type;
	const uint16_t *codes;
	uint32_t codes_count;
	uint32_t min_cutoff_milli; /* mHz: lower = smoother at rest, more lag */
	uint32_t beta_milli;       /* milli: higher = less lag on fast motion */
	uint32_t d_cutoff_milli;   /* mHz: derivative cutoff */
	bool ema_only;             /* true = fixed-alpha EMA (the simpler "A-arm" to try first) */
	uint32_t ema_alpha_milli;  /* 0..1000: EMA weight for the new sample (ema_only mode) */
};

struct one_euro_axis {
	bool inited;
	int64_t last_ms;
	float x_hat;  /* filtered value */
	float dx_hat; /* filtered derivative */
};

struct one_euro_data {
	struct one_euro_axis axis[ONE_EURO_MAX_CODES];
};

/* alpha = 1 / (1 + tau/Te),  tau = 1/(2*pi*cutoff) */
static inline float alpha_from_cutoff(float cutoff_hz, float te_s)
{
	if (cutoff_hz <= 0.0f) {
		return 1.0f;
	}
	float tau = 1.0f / (2.0f * 3.14159265f * cutoff_hz);
	return 1.0f / (1.0f + tau / te_s);
}

static int one_euro_handle_event(const struct device *dev, struct input_event *event,
				 uint32_t param1, uint32_t param2,
				 struct zmk_input_processor_state *state)
{
	const struct one_euro_config *cfg = dev->config;
	struct one_euro_data *data = dev->data;

	if (event->type != cfg->input_type) {
		return 0;
	}

	int idx = -1;
	for (uint32_t i = 0; i < cfg->codes_count; ++i) {
		if (cfg->codes[i] == event->code) {
			idx = (int)i;
			break;
		}
	}
	if (idx < 0) {
		return 0;
	}

	struct one_euro_axis *ax = &data->axis[idx];
	float x = (float)event->value;
	int64_t now_ms = k_uptime_get();

	if (!ax->inited) {
		ax->inited = true;
		ax->last_ms = now_ms;
		ax->x_hat = x;
		ax->dx_hat = 0.0f;
		return 0; /* pass first sample through unfiltered */
	}

	float te_s = (float)(now_ms - ax->last_ms) / 1000.0f;
	ax->last_ms = now_ms;
	if (te_s <= 0.0f) {
		te_s = 0.001f; /* guard against same-ms bursts */
	}

	if (cfg->ema_only) {
		float a = (float)cfg->ema_alpha_milli / 1000.0f;
		ax->x_hat = a * x + (1.0f - a) * ax->x_hat;
		event->value = (int32_t)lroundf(ax->x_hat);
		return 0;
	}

	float mincut = (float)cfg->min_cutoff_milli / 1000.0f;
	float beta = (float)cfg->beta_milli / 1000.0f;
	float dcut = (float)cfg->d_cutoff_milli / 1000.0f;

	float dx = (x - ax->x_hat) / te_s;
	float a_d = alpha_from_cutoff(dcut, te_s);
	ax->dx_hat = a_d * dx + (1.0f - a_d) * ax->dx_hat;

	float cutoff = mincut + beta * fabsf(ax->dx_hat);
	float a_x = alpha_from_cutoff(cutoff, te_s);
	ax->x_hat = a_x * x + (1.0f - a_x) * ax->x_hat;

	event->value = (int32_t)lroundf(ax->x_hat);
	return 0;
}

static const struct zmk_input_processor_driver_api one_euro_api = {
	.handle_event = one_euro_handle_event,
};

#define ONE_EURO_INST(n)                                                               \
	static const uint16_t one_euro_codes_##n[] = {INPUT_REL_X, INPUT_REL_Y};       \
	static const struct one_euro_config one_euro_cfg_##n = {                        \
		.input_type = DT_INST_PROP_OR(n, input_type, INPUT_EV_REL),            \
		.codes = one_euro_codes_##n,                                           \
		.codes_count = 2,                                                      \
		.min_cutoff_milli = DT_INST_PROP_OR(n, min_cutoff_milli, 1000),        \
		.beta_milli = DT_INST_PROP_OR(n, beta_milli, 7),                       \
		.d_cutoff_milli = DT_INST_PROP_OR(n, d_cutoff_milli, 1000),            \
		.ema_only = DT_INST_PROP(n, ema_only),                                 \
		.ema_alpha_milli = DT_INST_PROP_OR(n, ema_alpha_milli, 400),           \
	};                                                                             \
	static struct one_euro_data one_euro_data_##n;                                 \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, &one_euro_data_##n, &one_euro_cfg_##n,     \
			      POST_KERNEL, CONFIG_ZMK_INPUT_PROCESSOR_INIT_PRIORITY,   \
			      &one_euro_api);

DT_INST_FOREACH_STATUS_OKAY(ONE_EURO_INST)
