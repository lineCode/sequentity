#include <vector>
#include <array>

/**
 * @brief Components and Data generated by these tools
 *
 * Tools translate application events into Sequentity events.
 * For example, a mouse click on a given entity results in
 * the `Activated` component being assigned. This then causes
 * the relevant portion of a tool to generate a new track, new
 * channel and new event.
 *
 * During press-and-hold, this newly created event is then mutated
 * with additional data, such as where the mouse is over time.
 * Incrementing the length of the event to line up with the amount
 * of data generated by the input.
 *
 */

// An entity has just been made active
struct Activated { int time; };

// An entity is being interacted with, e.g. dragged with mouse
struct Active { int time; };

// An entity transitioned from active to deactive
struct Deactivated { int time; };

// Halt an ongoing iteration of enities with an `Active` component
struct Abort {};

struct Tooltip { const char* text; };

enum class ToolType : std::uint8_t {
    Select,
    DragSelect,
    LassoSelect,

    Translate,
    Rotate,
    Scale,

    Scrub,
};

struct Tool {
    ToolType type;
    std::function<void(bool record)> execute;
};

// struct ToolContext {
//     std::function<void()> preview;
//     std::function<void()> begin;
//     std::function<void()> update;
//     std::function<void()> finish;
// };

// From e.g. Wacom tablet
struct InputPressure  { float strength; };
struct InputPitch  { float angle; };
struct InputYaw  { float angle; };

// From e.g. Mouse or WASD keys
struct InputPosition2D  {
    Position absolute { 0, 0 };
    Position relative { 0, 0 };
    Position delta { 0, 0 };
};

struct InputPosition3D {
    Position absolute { 0, 0 };
    Position relative { 0, 0 };
};

// From e.g. WASD keys or D-PAD on XBox controller
enum class InputDirection2D : std::uint8_t { Left = 0, Up, Right, Down };
enum class InputDirection3D : std::uint8_t { Left = 0, Up, Right, Down, Forward, Backward };


/**
 * @brief Application data generated by these tools
 *
 * In addition to each Sequentity event, there is also our internal
 * application data, carried by events via a void*
 *
 */
struct TranslateEventData {
    Position offset;
    std::vector<Position> positions;
};

struct RotateEventData {
    std::vector<int> orientations;
};

struct ScaleEventData {
    std::vector<int> scales;
};

struct ScrubEventData {
    std::vector<int> deltas;
};

struct ToolEventData {
    ToolType type;
    std::unordered_map<int, InputPosition2D> input;
};


// Possible event types
enum EventType : Sequentity::EventType {
    InvalidEvent = 0,  // Catch uninitialised types

    SelectEvent,
    LassoSelectEvent,
    DragSelectEvent,

    TranslateEvent,
    RotateEvent,
    ScaleEvent,

    ScrubEvent,

    MousePressEvent,
    MouseMoveEvent,
    MouseReleaseEvent,
    KeyPressEvent,
    KeyReleaseEvent,

    ToolEvent,
};


struct ToolContext {
    virtual ToolType type() = 0;
    virtual void setup() {}
    virtual void begin() {}
    virtual void begin(entt::entity) {}
    virtual void update() {}
    virtual void update(InputPosition2D) {}
    virtual void update(entt::entity, InputPosition2D) {}
    virtual void preview() {}
    virtual void record(int time) {}
    virtual void finish() {}
    virtual void teardown() {}
};



struct SelectContext : public ToolContext {
    inline ToolType type() { return ToolType::Select; }
    void begin() override {
        Debug() << "Selecting..";
    }

    void update() override {}
    void finish() override {}
};


struct ScrubContext : public ToolContext {
    inline ToolType type() { return ToolType::Scrub; }
    void begin() override {
        Debug() << "Scrubbing..";
    }

    void update() override {}
    void finish() override {}
};


struct TranslateContext : public ToolContext {
    inline ToolType type() { return ToolType::Translate; }
    inline const char* name() { return "Translate"; }
    inline ImVec4 color() { return ImColor::HSV(0.0f, 0.75f, 0.75f); }

    void setup() {
        Debug() << "Setting Translate mouse cursor..";
        Debug() << "Setting Translate tool tips..";
    }

    void teardown() {
        // Handle case of user switching tool in the middle of updating
        if (_state == _Active) finish();
    }

    void begin(entt::entity entity) {
        _entity = entity;
        _state = _Activated;

        Registry.reset<Selected>();
        Registry.assign<Selected>(entity);
    }

    void update(entt::entity entity, InputPosition2D input) {
        begin(entity);
        update(input);
    }

    void update(InputPosition2D input) {
        Registry.reset<Tooltip>();

        if (Registry.valid(_entity)) {
            if (!Registry.has<MoveIntent>(_entity)) {
                Registry.assign<MoveIntent>(_entity, input.delta.x, input.delta.y);

            } else {
                auto& intent = Registry.get<MoveIntent>(_entity);
                intent.x += input.delta.x;
                intent.y += input.delta.y;
            }

            _state = _Active;
            _input = input;
        }

        else {
            // Let the user know what happens once clicked
            Registry.view<Hovered>().each([](auto entity, const auto) {
                Registry.assign<Tooltip>(entity, "Drag to translate");
            });

            _state = _None;
        }
    }

    void record(int time) {
        if (!_state) return;

        if (_state == _Activated) {
            _begin_time = time;

            auto [name, color] = Registry.get<Name, Color>(_entity);

            if (!Registry.has<Sequentity::Track>(_entity)) {
                Registry.assign<Sequentity::Track>(_entity, name.text, color);
            }

            auto* data = new ToolEventData{}; {
                data->type = ToolType::Translate;
            }

            auto& track = Registry.get<Sequentity::Track>(_entity);
            bool new_channel = !Sequentity::HasChannel(track, TranslateEvent);
            auto& channel = Sequentity::PushChannel(track, TranslateEvent);

            if (new_channel) {
                channel.label = this->name();
                channel.color = this->color();
            }

            Sequentity::PushEvent(channel, {
                time,                               /* time= */
                1,                                  /* length= */
                color,                              /* color= */

                // Store reference to our data
                TranslateEvent,                     /* type= */
                static_cast<void*>(data)            /* data= */
            });
        }

        if (_state == _Active) {
            if (_begin_time > time) {
                // Abort
                _state = _None;
                return;
            }

            auto& track = Registry.get<Sequentity::Track>(_entity);
            
            if (!track.channels.count(TranslateEvent)) {
                Warning() << "TranslateTool on" << track.label << "didn't have a TranslateEvent";
                return;
            }

            auto& channel = track.channels[TranslateEvent];
            auto& event = channel.events.back();

            auto data = static_cast<ToolEventData*>(event.data);

            // Update existing data
            data->input[time] = _input;
            event.length = time - event.time + 1;
        }

        if (_state == _Deactivated) {
            Debug() << "End";
        }
    }

    void finish() override {
        _entity = entt::null;
        _state = _Deactivated;
    }

private:
    entt::entity _entity { entt::null };
    InputPosition2D _input;
    int _begin_time { 0 };

    enum State_ : std::uint8_t {
        _None = 0,
        _Activated,
        _Active,
        _Deactivated
    } _state { _None };
};


struct RotateContext : public ToolContext {
    inline ToolType type() { return ToolType::Rotate; }
    RotateContext() {
        Debug() << "Rotate context established";
    }

    ~RotateContext() {
        Debug() << "Rotate context destroyed";
    }

    void begin() override {
        _is_active = true;
        Debug() << "Beginning!";
    }

    void update() override {
        if (_is_active) Debug() << "updating..";
    }

    void finish() override {
        _is_active = false;
        Debug() << "Finishing..";
    }

private:
    bool _is_active { false };
};


struct ScaleContext : public ToolContext {
    inline ToolType type() { return ToolType::Scale; }
    ScaleContext() {
        Debug() << "Scale context established";
    }

    ~ScaleContext() {
        Debug() << "Scale context destroyed";
    }

    void begin() override {
        _is_active = true;
        Debug() << "Beginning!";
    }

    void update() override {
        if (_is_active) Debug() << "updating..";
    }

    void finish() override {
        _is_active = false;
        Debug() << "Finishing..";
    }

private:
    bool _is_active { false };
};


TranslateContext copy(TranslateContext ctx) { return TranslateContext{}; }


/**
 * @brief The simplest possible tool
 *
 *
 */
static void SelectTool(bool record) {
    Registry.view<Name, Activated>().each([](auto entity, const auto& name, const auto&) {
        
        // Ensure there is only ever 1 selected entity
        Registry.reset<Selected>();

        Registry.assign<Selected>(entity);
    });
}


/**
 * @brief Translate an entity
 *
 *      __________ 
 *     |          |
 *     |          | ----------->   
 *     |          |
 *     |__________|
 *
 *
 */
static void TranslateTool(bool record) {
    // Handle press input of type: 2D range, relative anything with a position

    Registry.view<Name, Activated, InputPosition2D, Color, Position>().each([record](
                                                                      auto entity,
                                                                      const auto& name,
                                                                      const auto& state,
                                                                      const auto& input,
                                                                      const auto& color,
                                                                      const auto& position) {
        Registry.reset<Selected>();
        Registry.assign<Selected>(entity);

        if (!record) return;

        // The default name for any new track is coming from the owning entity
        if (!Registry.has<Sequentity::Track>(entity)) {
            Registry.assign<Sequentity::Track>(entity, name.text, color);
        }

        auto* data = new TranslateEventData{}; {
            data->positions.emplace_back(Position{});
        }

        auto& track = Registry.get<Sequentity::Track>(entity);
        bool has_channel = Sequentity::HasChannel(track, TranslateEvent);
        auto& channel = Sequentity::PushChannel(track, TranslateEvent);

        if (!has_channel) {
            channel.label = "Translate";
            channel.color = ImColor::HSV(0.0f, 0.75f, 0.75f);
        }

        Sequentity::PushEvent(channel, {
            state.time + 1,                     /* time= */
            1,                                  /* length= */
            color,                              /* color= */

            // Store reference to our data
            TranslateEvent,                     /* type= */
            static_cast<void*>(data)            /* data= */
        });
    });

    Registry.view<Active, InputPosition2D>(entt::exclude<Abort>).each([record](
                                                                auto entity,
                                                                const auto& state,
                                                                const auto& input) {
        if (!record) {
            Registry.assign<MoveIntent>(entity, input.delta.x, input.delta.y);
            return;
        }

        auto& track = Registry.get<Sequentity::Track>(entity);
        
        if (!track.channels.count(TranslateEvent)) {
            Warning() << "TranslateTool on" << track.label << "didn't have a TranslateEvent";
            return;
        }

        auto& channel = track.channels[TranslateEvent];
        auto& event = channel.events.back();

        auto index = state.time - event.time + 1;
        auto data = static_cast<TranslateEventData*>(event.data);

        // Update existing data
        auto value = input.delta;
        if (data->positions.size() > index) {
            data->positions[index] = input.delta;
        }

        // Append new data
        else {
            data->positions.emplace_back(input.delta);
        }

        event.length = index + 1;
    });

    Registry.view<Deactivated>().each([](auto entity, const auto&) {});
}


/**
 * @brief Rotate an entity
 *                  __
 *      __________     \
 *     |          |     v
 *     |          |   
 *     |          |
 *     |__________|
 *  ^
 *   \___
 *
 */
static void RotateTool(bool record) {
    Registry.view<Name, Activated, InputPosition2D, Color, Orientation>().each([](
                                                                         auto entity,
                                                                         const auto& name,
                                                                         const auto& state,
                                                                         const auto& input,
                                                                         const auto& color,
                                                                         const auto& orientation) {
        auto* data = new RotateEventData{}; {
            data->orientations.push_back(0);
        }

        if (!Registry.has<Sequentity::Track>(entity)) {
            Registry.assign<Sequentity::Track>(entity, name.text, color);
        }

        auto& track = Registry.get<Sequentity::Track>(entity);
        bool has_channel = track.channels.count(RotateEvent);
        auto& channel = track.channels[RotateEvent];

        if (!has_channel) {
            channel.label = "Rotate";
            channel.color = ImColor::HSV(0.33f, 0.75f, 0.75f);
        }

        Sequentity::PushEvent(channel, {
            state.time + 1,
            1,
            color,

            RotateEvent,
            static_cast<void*>(data)
        });

        Registry.reset<Selected>();
        Registry.assign<Selected>(entity);
    });

    Registry.view<Name, Active, InputPosition2D, Sequentity::Track>(entt::exclude<Abort>).each([](const auto& name,
                                                                           const auto& state,
                                                                           const auto& input,
                                                                           auto& track) {
        if (!track.channels.count(RotateEvent)) { Warning() << "RotateTool: This should never happen"; return; }

        auto& channel = track.channels[RotateEvent];
        auto& event = channel.events.back();

        auto index = state.time - event.time + 1;
        auto data = static_cast<RotateEventData*>(event.data);

        // Update existing data
        int value = input.delta.x;
        if (data->orientations.size() > index) {
            data->orientations[index] = value;
        }

        // Append new data
        else {
            data->orientations.emplace_back(value);
        }

        event.length = index + 1;
    });

    Registry.view<Deactivated>().each([](auto entity, const auto&) {});
}


/**
 * @brief Scale an entity
 *
 *   \              /
 *    \ __________ /
 *     |          |
 *     |          |
 *     |          |
 *     |__________|
 *    /            \
 *   /              \
 *
 */
static void ScaleTool(bool record) {
    Registry.view<Name, Activated, InputPosition2D, Color, Size>().each([](
                                                                         auto entity,
                                                                         const auto& name,
                                                                         const auto& state,
                                                                         const auto& input,
                                                                         const auto& color,
                                                                         const auto& size) {
        auto* data = new ScaleEventData{}; {
            data->scales.push_back(0);
        }

        if (!Registry.has<Sequentity::Track>(entity)) {
            Registry.assign<Sequentity::Track>(entity, name.text, color);
        }

        auto& track = Registry.get<Sequentity::Track>(entity);
        bool has_channel = track.channels.count(ScaleEvent);
        auto& channel = track.channels[ScaleEvent];

        if (!has_channel) {
            channel.label = "Scale";
            channel.color = ImColor::HSV(0.52f, 0.75f, 0.50f);
        }

        Sequentity::PushEvent(channel, {
            state.time + 1,
            1,
            color,

            ScaleEvent,
            static_cast<void*>(data)
        });

        Registry.reset<Selected>();
        Registry.assign<Selected>(entity);
    });

    Registry.view<Name, Active, InputPosition2D, Sequentity::Track>(entt::exclude<Abort>).each([](const auto& name,
                                                                           const auto& state,
                                                                           const auto& input,
                                                                           auto& track) {
        if (!track.channels.count(ScaleEvent)) { Warning() << "ScaleTool: This should never happen"; return; }

        auto& channel = track.channels[ScaleEvent];
        auto& event = channel.events.back();

        auto index = state.time - event.time + 1;
        auto data = static_cast<ScaleEventData*>(event.data);

        // Update existing data
        auto value = input.delta.x;
        if (data->scales.size() > index) {
            data->scales[index] = value;
        }

        // Append new data
        else {
            data->scales.emplace_back(value);
        }

        event.length = index + 1;
    });

    Registry.view<Deactivated>().each([](auto entity, const auto&) {});
}


/**
 * @brief Relatively move the timeline
 *
 * This tool differs from the others, in that it doesn't actually apply to the
 * active entity. Instead, it applies to the global state of Sequentity. But,
 * it currently can't do that, unless an entity is active. So that's a bug.
 *
 */
static void ScrubTool(bool record) {
    // Press
    static int previous_time { 0 };
    Registry.view<Activated, InputPosition2D>().each([](const auto& activated, const auto& input) {
        auto& state = Registry.ctx<Sequentity::State>();
        previous_time = state.current_time;
    });

    // Hold
    Registry.view<Active, InputPosition2D>().each([](const auto&, const auto& input) {
        auto& state = Registry.ctx<Sequentity::State>();
        state.current_time = previous_time + input.relative.x / 10;
    });

    // Release
    Registry.view<Deactivated>().each([](auto entity, const auto&) {});
}
