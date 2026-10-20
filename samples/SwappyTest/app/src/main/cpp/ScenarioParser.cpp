/*
 * Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ScenarioParser.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ScenarioBuilder.h"
#include "json11/json11.hpp"

namespace dvfs {

namespace {

using json11::Json;

constexpr double kNsPerMs = 1e6;
constexpr double kNsPerSecond = 1e9;
// Keeps quantities well inside int64_t once converted to nanoseconds.
constexpr double kMaxNs = 1e18;

// Alternative field names and the field each one stands for.
const std::map<std::string, std::string> kAliases = {
        {"cpu_ms", "cpu"},          {"gpu_ms", "gpu"},  {"boost_ms", "boost"}, {"f", "frames"},
        {"duration_s", "duration"}, {"ops", "actions"}, {"step", "name"},      {"id", "title"},
        {"scenario", "title"},
};

// The fields of a step, of an item of "actions" and of a repeat body.
const std::set<std::string> kActionFields = {
        "name",
        "auto_pipeline_mode",
        "auto_swap_interval",
        "buffer_stuffing_fix_wait",
        "swap_interval",
        "cpu",
        "gpu",
        "reset_pacing",
        "boost",
        "frames",
        "duration",
        "repeat",
        "actions",
};

// The header holds the scenario title and description and the actions that
// run before the first step.
const std::set<std::string> kHeaderFields = [] {
    std::set<std::string> fields = kActionFields;
    fields.insert({"title", "description"});
    return fields;
}();

// The object form of repeat: {"repeat": {"count" or "duration": X, "actions": [...]}}.
const std::set<std::string> kRepeatFields = {"count", "duration", "actions"};

std::string canonical_name(const std::string& field) {
    const auto alias = kAliases.find(field);
    return alias == kAliases.end() ? field : alias->second;
}

// Whether the object is a step rather than a header of settings.
bool plays_frames(const Json::object& object) {
    for (const auto& [field, value] : object) {
        const std::string name = canonical_name(field);
        if (name == "frames" || name == "duration" || name == "repeat" || name == "actions" ||
            name == "name") {
            return true;
        }
    }
    return false;
}

bool is_blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool is_whole(double value) {
    return value == std::floor(value);
}

std::string default_description(const std::string& spec, size_t step_count) {
    return "CUSTOM (" + std::to_string(step_count) +
            " steps): " + (is_blank(spec) ? "<empty spec>" : spec);
}

std::string index_path(const std::string& path, size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

// kCount marks a plain number: a frame or repeat count, or a value in the
// default unit of a field.
enum class Unit { kCount, kNanoseconds, kMilliseconds, kSeconds, kFps };

struct Quantity {
    double value;
    Unit unit;
};

// Whether value in unit is finite and fits kMaxNs once converted to nanoseconds.
bool fits_in_ns(double value, Unit unit) {
    double ns = value;
    if (unit == Unit::kSeconds) ns = value * kNsPerSecond;
    if (unit == Unit::kMilliseconds) ns = value * kNsPerMs;
    if (unit == Unit::kFps) ns = kNsPerSecond / value;
    return std::fabs(ns) <= kMaxNs;
}

// Reads a number in default_unit or a string such as "16.6ms", "2s", "500ns"
// or "60fps"; a string without a unit is in default_unit. Returns false when
// the value is neither.
bool parse_quantity(const Json& json, Unit default_unit, Quantity* quantity) {
    if (json.is_number()) {
        if (!fits_in_ns(json.number_value(), default_unit)) return false;
        *quantity = {json.number_value(), default_unit};
        return true;
    }
    if (!json.is_string()) return false;

    static const std::pair<const char*, Unit> kUnits[] = {
            {"fps", Unit::kFps},
            {"ns", Unit::kNanoseconds},
            {"ms", Unit::kMilliseconds},
            {"s", Unit::kSeconds},
    };
    std::string number = json.string_value();
    Unit unit = default_unit;
    for (const auto& [suffix, suffix_unit] : kUnits) {
        const size_t length = std::strlen(suffix);
        if (number.size() > length && number.compare(number.size() - length, length, suffix) == 0) {
            number.resize(number.size() - length);
            unit = suffix_unit;
            break;
        }
    }
    char* end = nullptr;
    const double value = std::strtod(number.c_str(), &end);
    if (number.empty() || *end != '\0') return false;
    if (unit == Unit::kFps && value <= 0) return false;
    if (!fits_in_ns(value, unit)) return false;
    *quantity = {value, unit};
    return true;
}

std::chrono::nanoseconds to_ns(const Quantity& quantity) {
    switch (quantity.unit) {
        case Unit::kFps:
            return std::chrono::nanoseconds(std::llround(kNsPerSecond / quantity.value));
        case Unit::kSeconds:
            return std::chrono::nanoseconds(std::llround(quantity.value * kNsPerSecond));
        case Unit::kMilliseconds:
            return std::chrono::nanoseconds(std::llround(quantity.value * kNsPerMs));
        case Unit::kNanoseconds:
        case Unit::kCount:
            break;
    }
    return std::chrono::nanoseconds(std::llround(quantity.value));
}

std::chrono::milliseconds to_ms(const Quantity& quantity) {
    return std::chrono::milliseconds(std::llround(to_ns(quantity).count() / kNsPerMs));
}

bool parse_pipeline_mode(const Json& json, ScenarioPipelineMode* mode) {
    if (json.is_bool()) {
        *mode = json.bool_value() ? ScenarioPipelineMode::Auto : ScenarioPipelineMode::Off;
        return true;
    }
    const std::string& text = json.string_value();
    if (text == "on") {
        *mode = ScenarioPipelineMode::On;
    } else if (text == "off") {
        *mode = ScenarioPipelineMode::Off;
    } else if (text == "auto") {
        *mode = ScenarioPipelineMode::Auto;
    } else {
        return false;
    }
    return true;
}

bool parse_on_off(const Json& json, bool* enabled) {
    if (json.is_bool()) {
        *enabled = json.bool_value();
        return true;
    }
    if (json.string_value() != "on" && json.string_value() != "off") return false;
    *enabled = json.string_value() == "on";
    return true;
}

// Builds a custom scenario from its JSON spec, keeping the location and
// message of the first error.
class Parser {
public:
    // Parses spec into *scenario. Returns false on the first error.
    bool parse(const std::string& spec, Scenario* scenario);

    const std::string& where() const {
        return mWhere;
    }
    const std::string& message() const {
        return mMessage;
    }

private:
    bool read_header_and_steps(const std::string& spec, Json::object* header, Json::array* steps);
    bool normalize(const Json& json, const std::string& path, const std::set<std::string>& fields,
                   Json::object* object);
    bool apply_action(const Json::object& action, const std::string& path);
    bool fail(std::string where, std::string message);

    ScenarioBuilder mBuilder;
    std::string mWhere;
    std::string mMessage;
};

bool Parser::parse(const std::string& spec, Scenario* scenario) {
    Json::object header;
    Json::array steps;
    if (!read_header_and_steps(spec, &header, &steps)) return false;
    if (!normalize(header, "header", kHeaderFields, &header)) return false;
    if (plays_frames(header)) {
        return fail("header", "frames, duration, repeat, actions and name belong in a step");
    }

    std::string title = "CUSTOM";
    std::string description = default_description(spec, steps.size());
    const auto take_string = [&](const char* field, std::string* value) {
        const auto text = header.find(field);
        if (text == header.end()) return true;
        if (!text->second.is_string()) {
            return fail("header", std::string(field) + " must be a string");
        }
        *value = text->second.string_value();
        header.erase(text);
        return true;
    };
    if (!take_string("title", &title) || !take_string("description", &description)) return false;
    mBuilder = ScenarioBuilder(std::move(title), std::move(description));
    if (!apply_action(header, "header")) return false;
    if (!mBuilder.ok()) return fail("header", mBuilder.error());

    for (size_t i = 0; i < steps.size(); ++i) {
        const std::string path = index_path("steps", i);
        Json::object step;
        if (!normalize(steps[i], path, kActionFields, &step)) return false;
        // A step without a name after the first one still starts a new step.
        if (i > 0) step.emplace("name", "");
        if (!apply_action(step, path)) return false;
        if (!mBuilder.ok()) return fail(path, mBuilder.error());
    }
    if (!mBuilder.finish()) {
        return fail(steps.empty() ? "header" : index_path("steps", steps.size() - 1),
                    mBuilder.error());
    }
    *scenario = mBuilder.build();
    return true;
}

// Brings every accepted shape of spec into a header and its steps: an object
// with "steps", an array of steps, or a stream of objects whose first object
// is the header unless it plays frames.
bool Parser::read_header_and_steps(const std::string& spec, Json::object* header,
                                   Json::array* steps) {
    if (is_blank(spec)) return true;
    std::string::size_type stop_position = 0;
    std::string error;
    const std::vector<Json> values =
            Json::parse_multi(spec, stop_position, error, json11::JsonParse::COMMENTS);
    if (!error.empty()) {
        const auto line = 1 + std::count(spec.begin(), spec.begin() + stop_position, '\n');
        return fail("L" + std::to_string(line), error);
    }
    if (values.size() == 1 && values[0].is_array()) {
        *steps = values[0].array_items();
        return true;
    }
    if (!values[0].is_object()) return fail("spec", "expected a JSON object or array");

    const Json::object& first = values[0].object_items();
    const bool first_is_header = first.count("steps") > 0 || !plays_frames(first);
    if (first_is_header) *header = first;
    steps->assign(values.begin() + (first_is_header ? 1 : 0), values.end());

    const auto header_steps = header->find("steps");
    if (header_steps == header->end()) return true;
    if (!steps->empty()) return fail("header", "steps cannot be followed by more objects");
    if (!header_steps->second.is_array()) return fail("header", "steps must be an array");
    *steps = header_steps->second.array_items();
    header->erase(header_steps);
    return true;
}

// Copies the fields of json into *object under their canonical names and
// hoists the object form of repeat into "repeat" and "actions".
bool Parser::normalize(const Json& json, const std::string& path,
                       const std::set<std::string>& fields, Json::object* object) {
    if (!json.is_object()) return fail(path, "expected a JSON object");
    Json::object normalized;
    std::map<std::string, std::string> given_as;
    for (const auto& [field, value] : json.object_items()) {
        const std::string name = canonical_name(field);
        if (fields.count(name) == 0) return fail(path, "unknown field '" + field + "'");
        if (!given_as.emplace(name, field).second) {
            return fail(path, "'" + given_as[name] + "' and '" + field + "' are the same field");
        }
        normalized[name] = value;
    }

    const auto repeat = normalized.find("repeat");
    if (repeat != normalized.end() && repeat->second.is_object()) {
        const std::string repeat_path = path + ".repeat";
        Json::object body;
        if (!normalize(repeat->second, repeat_path, kRepeatFields, &body)) return false;
        const auto count = body.find("count");
        const auto duration = body.find("duration");
        if ((count == body.end()) == (duration == body.end())) {
            return fail(repeat_path, "needs either count or duration");
        }
        if (body.count("actions") > 0) {
            if (normalized.count("actions") > 0) return fail(path, "actions given twice");
            normalized["actions"] = body["actions"];
        }
        if (count != body.end()) {
            if (!count->second.is_number()) return fail(repeat_path, "count must be a number");
            repeat->second = count->second;
        } else {
            // A string with a unit is what repeat reads as a duration.
            Quantity quantity;
            if (!parse_quantity(duration->second, Unit::kSeconds, &quantity)) {
                return fail(repeat_path, "bad duration value " + duration->second.dump());
            }
            repeat->second = std::to_string(to_ns(quantity).count()) + "ns";
        }
    }
    *object = std::move(normalized);
    return true;
}

// Applies settings, then loads, then one-shot actions, then frames, then the
// actions, inside a repeat block when the action repeats.
bool Parser::apply_action(const Json::object& action, const std::string& path) {
    const auto field = [&action](const char* name) -> const Json* {
        const auto value = action.find(name);
        return value == action.end() ? nullptr : &value->second;
    };
    Quantity quantity;
    const auto read_quantity = [&](const char* name, Unit default_unit) {
        if (parse_quantity(action.at(name), default_unit, &quantity)) return true;
        return fail(path, "bad " + std::string(name) + " value " + action.at(name).dump());
    };

    if (const Json* name = field("name")) {
        if (!name->is_string()) return fail(path, "name must be a string");
        mBuilder.step(name->string_value());
    }
    if (const Json* value = field("auto_pipeline_mode")) {
        ScenarioPipelineMode mode;
        if (!parse_pipeline_mode(*value, &mode)) {
            return fail(path, "auto_pipeline_mode must be on, off or auto");
        }
        mBuilder.set_auto_pipeline_mode(mode);
    }
    if (const Json* value = field("auto_swap_interval")) {
        bool enabled;
        if (!parse_on_off(*value, &enabled)) {
            return fail(path, "auto_swap_interval must be true, false, on or off");
        }
        mBuilder.set_auto_swap_interval(enabled);
    }
    if (const Json* value = field("buffer_stuffing_fix_wait")) {
        if (!value->is_number() || !is_whole(value->number_value()) ||
            !(std::fabs(value->number_value()) <= std::numeric_limits<int>::max())) {
            return fail(path, "buffer_stuffing_fix_wait must be an integer");
        }
        mBuilder.set_buffer_stuffing_fix_wait(value->int_value());
    }
    if (field("swap_interval")) {
        if (!read_quantity("swap_interval", Unit::kNanoseconds)) return false;
        mBuilder.set_swap_interval_ns(to_ns(quantity));
    }

    if (field("cpu")) {
        if (!read_quantity("cpu", Unit::kMilliseconds)) return false;
        mBuilder.cpu(to_ns(quantity));
    }
    if (field("gpu")) {
        if (!read_quantity("gpu", Unit::kMilliseconds)) return false;
        mBuilder.gpu(to_ns(quantity));
    }

    if (const Json* value = field("reset_pacing")) {
        if (!value->is_bool()) return fail(path, "reset_pacing must be true or false");
        if (value->bool_value()) mBuilder.reset_frame_pacing();
    }
    if (field("boost")) {
        if (!read_quantity("boost", Unit::kMilliseconds)) return false;
        mBuilder.boost(to_ms(quantity));
    }

    if (field("frames")) {
        if (!read_quantity("frames", Unit::kCount)) return false;
        if (quantity.unit != Unit::kCount) {
            mBuilder.frames_for(to_ns(quantity));
        } else if (is_whole(quantity.value)) {
            mBuilder.frames(std::llround(quantity.value));
        } else {
            return fail(path, "frames must be a whole number of frames, use duration for seconds");
        }
    }
    if (field("duration")) {
        if (!read_quantity("duration", Unit::kSeconds)) return false;
        mBuilder.frames_for(to_ns(quantity));
    }

    if (!mBuilder.ok()) return fail(path, mBuilder.error());

    const bool repeats = field("repeat") != nullptr;
    if (repeats) {
        if (!read_quantity("repeat", Unit::kCount)) return false;
        if (quantity.unit != Unit::kCount) {
            mBuilder.repeat_for(to_ns(quantity));
        } else if (is_whole(quantity.value)) {
            mBuilder.repeat(std::llround(quantity.value));
        } else {
            return fail(path, "repeat must be a whole count or a duration with a unit");
        }
    }
    if (const Json* actions = field("actions")) {
        if (!actions->is_array()) return fail(path, "actions must be an array");
        for (size_t i = 0; i < actions->array_items().size(); ++i) {
            const std::string action_path = index_path(path + ".actions", i);
            Json::object nested;
            if (!normalize(actions->array_items()[i], action_path, kActionFields, &nested) ||
                !apply_action(nested, action_path)) {
                return false;
            }
            if (!mBuilder.ok()) return fail(action_path, mBuilder.error());
        }
    }
    if (repeats) mBuilder.end_repeat();
    return true;
}

bool Parser::fail(std::string where, std::string message) {
    mWhere = std::move(where);
    mMessage = std::move(message);
    return false;
}

} // namespace

std::string parse_custom_scenario(const std::string& spec, Scenario* scenario) {
    Parser parser;
    if (!parser.parse(spec, scenario)) {
        *scenario = Scenario("CUSTOM", default_description(spec, 0), {}, {});
        return "bad_custom_scenario:" + parser.where() + ": " + parser.message();
    }
    return scenario->step_count() == 0 ? "empty_custom_scenario" : "";
}

} // namespace dvfs
