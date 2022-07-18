#include "esphome/core/log.h"
#include "tuya_light.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace tuya {

static const char *const TAG = "tuya.light";

void TuyaLight::setup() {
  if (this->color_temperature_id_.has_value()) {
    this->parent_->register_listener(*this->color_temperature_id_, [this](const TuyaDatapoint &datapoint) {
      if (this->state_->current_values != this->state_->remote_values) {
        ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
        return;
      }

      auto datapoint_value = datapoint.value_uint;
      if (this->color_temperature_invert_) {
        datapoint_value = this->color_temperature_max_value_ - datapoint_value;
      }
      auto call = this->state_->make_call();
      call.set_color_temperature(this->cold_white_temperature_ +
                                 (this->warm_white_temperature_ - this->cold_white_temperature_) *
                                     (float(datapoint_value) / this->color_temperature_max_value_));
      call.perform();
    });
  }
  if (this->dimmer_id_.has_value()) {
    this->parent_->register_listener(*this->dimmer_id_, [this](const TuyaDatapoint &datapoint) {
      if (this->state_->current_values != this->state_->remote_values) {
        ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
        return;
      }

      auto call = this->state_->make_call();
      call.set_brightness(float(datapoint.value_uint) / this->max_value_);
      call.perform();
    });
  }
  if (switch_id_.has_value()) {
    this->parent_->register_listener(*this->switch_id_, [this](const TuyaDatapoint &datapoint) {
      if (this->state_->current_values != this->state_->remote_values) {
        ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
        return;
      }

      auto call = this->state_->make_call();
      call.set_state(datapoint.value_bool);
      call.perform();
    });
  }
  if (rgb_id_.has_value()) {
    this->parent_->register_listener(*this->rgb_id_, [this](const TuyaDatapoint &datapoint) {
      auto red = parse_hex<uint8_t>(datapoint.value_string.substr(0, 2));
      auto green = parse_hex<uint8_t>(datapoint.value_string.substr(2, 2));
      auto blue = parse_hex<uint8_t>(datapoint.value_string.substr(4, 2));
      if (red.has_value() && green.has_value() && blue.has_value()) {
        if (this->state_->current_values != this->state_->remote_values) {
          ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
          return;
        }

        auto call = this->state_->make_call();
        call.set_rgb(float(*red) / 255, float(*green) / 255, float(*blue) / 255);
        call.perform();
      }
    });
  } else if (hsv_id_.has_value()) {
    this->parent_->register_listener(*this->hsv_id_, [this](const TuyaDatapoint &datapoint) {
      auto hue = parse_hex<uint16_t>(datapoint.value_string.substr(0, 4));
      auto saturation = parse_hex<uint16_t>(datapoint.value_string.substr(4, 4));
      auto value = parse_hex<uint16_t>(datapoint.value_string.substr(8, 4));
      if (hue.has_value() && saturation.has_value() && value.has_value()) {
        if (this->state_->current_values != this->state_->remote_values) {
          ESP_LOGD(TAG, "Light is transitioning, datapoint change ignored");
          return;
        }

        float red, green, blue;
        hsv_to_rgb(*hue, float(*saturation) / 1000, float(*value) / 1000, red, green, blue);
        auto call = this->state_->make_call();
        call.set_rgb(red, green, blue);
        call.perform();
      }
    });
  }
  if (min_value_datapoint_id_.has_value()) {
    parent_->set_integer_datapoint_value(*this->min_value_datapoint_id_, this->min_value_);
  }
}

void TuyaLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya Dimmer:");
  if (this->dimmer_id_.has_value())
    ESP_LOGCONFIG(TAG, "   Dimmer has datapoint ID %u", *this->dimmer_id_);
  if (this->switch_id_.has_value())
    ESP_LOGCONFIG(TAG, "   Switch has datapoint ID %u", *this->switch_id_);
  if (this->color_id_.has_value()) {
    ESP_LOGCONFIG(TAG, "   Color has datapoint ID %u", *this->color_id_);
  } else if (this->rgb_id_.has_value()) {
    ESP_LOGCONFIG(TAG, "   RGB has datapoint ID %u", *this->rgb_id_);
  } else if (this->hsv_id_.has_value()) {
    ESP_LOGCONFIG(TAG, "   HSV has datapoint ID %u", *this->hsv_id_);
  }
}

light::LightTraits TuyaLight::get_traits() {
  auto has_color = this->color_id_.has_value() || this->rgb_id_.has_value() || this->hsv_id_.has_value();
  auto traits = light::LightTraits();
  if (this->color_temperature_id_.has_value() && this->dimmer_id_.has_value()) {
    if (has_color) {
      if (this->color_interlock_) {
        traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLOR_TEMPERATURE});
      } else {
        traits.set_supported_color_modes(
            {light::ColorMode::RGB_COLOR_TEMPERATURE, light::ColorMode::COLOR_TEMPERATURE});
      }
    } else
      traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
    traits.set_min_mireds(this->cold_white_temperature_);
    traits.set_max_mireds(this->warm_white_temperature_);
  } else if (has_color) {
    if (this->dimmer_id_.has_value()) {
      if (this->color_interlock_) {
        traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::WHITE});
      } else {
        traits.set_supported_color_modes({light::ColorMode::RGB_WHITE});
      }
    } else
      traits.set_supported_color_modes({light::ColorMode::RGB});
  } else if (this->dimmer_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  } else {
    traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  }
  return traits;
}

void TuyaLight::setup_state(light::LightState *state) { state_ = state; }

void TuyaLight::write_state(light::LightState *state) {
  auto has_color = this->color_id_.has_value() || this->rgb_id_.has_value() || this->hsv_id_.has_value();
  float red = 0.0f, green = 0.0f, blue = 0.0f;
  float color_temperature = 0.0f, brightness = 0.0f;

  if (has_color) {
    if (this->color_temperature_id_.has_value()) {
      state->current_values_as_rgbct(&red, &green, &blue, &color_temperature, &brightness);
    } else if (this->dimmer_id_.has_value()) {
      state->current_values_as_rgbw(&red, &green, &blue, &brightness);
    } else {
      state->current_values_as_rgb(&red, &green, &blue);
    }
  } else if (this->color_temperature_id_.has_value()) {
    state->current_values_as_ct(&color_temperature, &brightness);
  } else {
    state->current_values_as_brightness(&brightness);
  }

  if (!state->current_values.is_on() && this->switch_id_.has_value()) {
    parent_->set_boolean_datapoint_value(*this->switch_id_, false);
    return;
  }

  if (brightness > 0.0f || !color_interlock_) {
    if (this->color_temperature_id_.has_value()) {
      uint32_t color_temp_int = static_cast<uint32_t>(color_temperature * this->color_temperature_max_value_);
      if (this->color_temperature_invert_) {
        color_temp_int = this->color_temperature_max_value_ - color_temp_int;
      }
      parent_->set_integer_datapoint_value(*this->color_temperature_id_, color_temp_int);
    }

    if (this->dimmer_id_.has_value()) {
      auto brightness_int = static_cast<uint32_t>(brightness * this->max_value_);
      brightness_int = std::max(brightness_int, this->min_value_);

      parent_->set_integer_datapoint_value(*this->dimmer_id_, brightness_int);
    }
  }

  if (has_color && (brightness == 0.0f || !color_interlock_)) {
    uint8_t dp_id;
    TuyaColorType color_type;
    if (this->color_id_.has_value()) {
      dp_id = *this->color_id_;
      color_type = *this->color_type_;
    } else if (this->rgb_id_.has_value()) {
      dp_id = *this->rgb_id_;
      color_type = TuyaColorType::RGB;
    } else if (this->hsv_id_.has_value()) {
      dp_id = *this->hsv_id_;
      color_type = TuyaColorType::HSV;
    }

    std::string color_value;
    switch (color_type) {
      case TuyaColorType::RGB: {
        char buffer[7];
        sprintf(buffer, "%02X%02X%02X", int(red * 255), int(green * 255), int(blue * 255));
        color_value = buffer;
        break;
      }
      case TuyaColorType::HSV: {
        int hue;
        float saturation, value;
        rgb_to_hsv(red, green, blue, hue, saturation, value);
        char buffer[13];
        sprintf(buffer, "%04X%04X%04X", hue, int(saturation * 1000), int(value * 1000));
        color_value = buffer;
        break;
      }
      case TuyaColorType::RGBHSV: {
        int hue;
        float saturation, value;
        rgb_to_hsv(red, green, blue, hue, saturation, value);
        char buffer[15];
        sprintf(buffer, "%02X%02X%02X%04X%02X%02X", int(red * 255), int(green * 255), int(blue * 255), hue, int(saturation * 255), int(value * 255));
        color_value = buffer;
        break;
      }
    }
    this->parent_->set_string_datapoint_value(*this->color_id_, color_value);
  }

  if (this->switch_id_.has_value()) {
    parent_->set_boolean_datapoint_value(*this->switch_id_, true);
  }
}

}  // namespace tuya
}  // namespace esphome
