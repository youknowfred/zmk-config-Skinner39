# Raw HID support for ZMK

This [ZMK module](https://zmk.dev/docs/features/modules) enables Raw HID functionality for ZMK.

## Features

It allows bidirectional communication between host computer and keyboard over HID interface. Both USB and BT connections are supported.

Example usage - [display widget](https://github.com/zzeneg/zmk-nice-view-hid) with HID support, which can accept data from computer using [companion app](https://github.com/zzeneg/qmk-hid-host) and display current time/layout/volume.

## Installation

> [!WARNING]
> Enabling this module alone does not bring any functionality by default. You need another module or custom code to consume events, and companion app to send them.

To use, first add this module to your `config/west.yml` by adding a new entry to `remotes` and `projects`:

```yaml west.yml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: zzeneg # <-- new entry
      url-base: https://github.com/zzeneg
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-raw-hid # <-- new entry
      remote: zzeneg
      revision: main
  self:
    path: config
```

For more information, including instructions for building locally, check out the ZMK docs on [building with modules](https://zmk.dev/docs/features/modules#building-with-modules).

Add the `raw_hid_adapter` as an additional shield to your build, e.g. in `build.yaml`:

```yaml build.yaml
---
include:
  - board: nice_nano_v2
    shield: molekula raw_hid_adapter
```

## Adding support in other modules

Subscribe to `raw_hid_received_event` and implement your own listener:

```c
static int raw_hid_received_event_listener(const zmk_event_t *eh) {
    struct raw_hid_received_event *event = as_raw_hid_received_event(eh);
    if (event) {
        // do something
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(process_raw_hid_event, raw_hid_received_event_listener);
ZMK_SUBSCRIPTION(process_raw_hid_event, raw_hid_received_event);
```

## Configuration

| Name                         | Description                       | Default |
| ---------------------------- | --------------------------------- | ------- |
| `CONFIG_RAW_HID`             | Enable Raw HID module             | n       |
| `CONFIG_RAW_HID_USAGE_PAGE`  | HID Usage Page                    | 0xFF60  |
| `CONFIG_RAW_HID_USAGE`       | HID Usage                         | 0x61    |
| `CONFIG_RAW_HID_REPORT_SIZE` | HID Report size (number of bytes) | 32      |
| `CONFIG_RAW_HID_DEVICE`      | New HID device name               | HID_1   |

## Thanks

This module is heavily based on [badjeff's zmk-hid-io](https://github.com/badjeff/zmk-hid-io).
