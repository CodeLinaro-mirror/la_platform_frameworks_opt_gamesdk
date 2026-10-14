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

#include <gtest/gtest.h>

#include <chrono>
#include <utility>
#include <vector>

#include "Scenario.h"
#include "ScenarioBuilder.h"
#include "ScenarioEngine.h"
#include "scenario_test_util.h"

namespace dvfs {
namespace {

using namespace std::chrono_literals;

// Plays a started engine on the fake clock from start_ns until the scenario
// finishes or max_frames frames.
std::vector<ScenarioEngine::Frame> play_started(ScenarioEngine* engine, uint64_t start_ns,
                                                size_t max_frames = 100000) {
    std::vector<ScenarioEngine::Frame> frames;
    uint64_t now_ns = start_ns;
    while (frames.size() < max_frames) {
        const ScenarioEngine::Frame frame = engine->next_frame(now_ns);
        if (frame.finished) break;
        frames.push_back(frame);
        now_ns += kFakeFrameNs;
    }
    return frames;
}

// The frame indices before which a reset_pacing action fires.
std::vector<size_t> reset_pacing_frames(const std::vector<ScenarioEngine::Frame>& frames) {
    std::vector<size_t> indices;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].reset_frame_pacing) indices.push_back(i);
    }
    return indices;
}

// The frame indices before which a boost action fires.
std::vector<size_t> boost_frames(const std::vector<ScenarioEngine::Frame>& frames) {
    std::vector<size_t> indices;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].boost_ms > 0) indices.push_back(i);
    }
    return indices;
}

// Checks that actual plays the same frames as expected.
void expect_same_frames(const std::vector<ScenarioEngine::Frame>& actual,
                        const std::vector<ScenarioEngine::Frame>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        EXPECT_EQ(actual[i].step_index, expected[i].step_index) << "frame " << i;
        EXPECT_EQ(actual[i].cpu_ms, expected[i].cpu_ms) << "frame " << i;
        EXPECT_EQ(actual[i].gpu_ms, expected[i].gpu_ms) << "frame " << i;
        EXPECT_EQ(actual[i].reset_frame_pacing, expected[i].reset_frame_pacing) << "frame " << i;
        EXPECT_EQ(actual[i].boost_ms, expected[i].boost_ms) << "frame " << i;
    }
}

TEST(ScenarioEngineTest, FrameCountsAndDurations) {
    EXPECT_EQ(play(ScenarioBuilder().frames()).size(), 1u);
    EXPECT_EQ(play(ScenarioBuilder().frames(1)).size(), 1u);
    EXPECT_EQ(play(ScenarioBuilder().frames(4)).size(), 4u);
    EXPECT_EQ(play(ScenarioBuilder().frames_for(50ms)).size(), 5u);
    EXPECT_EQ(play(ScenarioBuilder().frames_for(3s)).size(), 300u);
    EXPECT_EQ(play(ScenarioBuilder().frames_for(1s).frames(2).frames()).size(), 103u);
}

TEST(ScenarioEngineTest, SettersApplyBeforeTheFramesTheyPrecede) {
    // The catalog idiom: each command sets what it plays, nothing is restored.
    ScenarioBuilder builder;
    builder.gpu(7ms);
    builder.step("a").cpu(10ms).frames(2).cpu(24ms).frames();
    builder.step("b").frames();
    const Scenario scenario = built(builder);
    EXPECT_EQ(cpu_sequence(scenario), (std::vector<double>{10, 10, 24, 24}));
    EXPECT_EQ(gpu_sequence(scenario), (std::vector<double>{7, 7, 7, 7}));

    ScenarioBuilder sub_ms;
    sub_ms.cpu(500us).gpu(1500us).frames();
    const Scenario sub_ms_scenario = built(sub_ms);
    EXPECT_EQ(cpu_sequence(sub_ms_scenario), (std::vector<double>{0.5}));
    EXPECT_EQ(gpu_sequence(sub_ms_scenario), (std::vector<double>{1.5}));
}

TEST(ScenarioEngineTest, NestedCountRepeats) {
    ScenarioBuilder builder;
    builder.repeat(2, [](ScenarioBuilder& body) {
        body.cpu(1ms).frames();
        body.repeat(3, [](ScenarioBuilder& inner) { inner.cpu(2ms).frames(); });
    });
    builder.cpu(3ms).frames();
    EXPECT_EQ(cpu_sequence(builder), (std::vector<double>{1, 2, 2, 2, 1, 2, 2, 2, 3}));
    EXPECT_EQ(cpu_sequence(ScenarioBuilder().repeat(1).cpu(1ms).frames().end_repeat()),
              (std::vector<double>{1}));
}

TEST(ScenarioEngineTest, TimedRepeatStopsMidIteration) {
    // 250 ms hold 25 frames: six whole 4-frame iterations and the first frame
    // of the seventh.
    ScenarioBuilder builder;
    builder.repeat_for(250ms, [](ScenarioBuilder& body) {
        body.cpu(1ms).frames().cpu(2ms).frames().cpu(3ms).frames().cpu(4ms).frames();
    });
    builder.cpu(9ms).frames();
    std::vector<double> expected;
    for (int i = 0; i < 6; ++i) expected.insert(expected.end(), {1, 2, 3, 4});
    expected.push_back(1);
    expected.push_back(9);
    EXPECT_EQ(cpu_sequence(builder), expected);
}

TEST(ScenarioEngineTest, OuterTimeLimitEndsInnerCountBlocksAndFrames) {
    // 100 ms hold 10 frames: the inner count block and the frames(50) after
    // it end with the outer block, and the scenario goes on after it.
    std::vector<double> expected(10, 1);
    expected.push_back(3);
    ScenarioBuilder nested;
    nested.repeat_for(100ms, [](ScenarioBuilder& body) {
        body.repeat(100, [](ScenarioBuilder& inner) { inner.cpu(1ms).frames(); });
        body.cpu(2ms).frames(50);
    });
    nested.cpu(3ms).frames();
    EXPECT_EQ(cpu_sequence(nested), expected);
    EXPECT_EQ(cpu_sequence(ScenarioBuilder()
                                   .repeat_for(100ms)
                                   .cpu(1ms)
                                   .frames(100)
                                   .end_repeat()
                                   .cpu(3ms)
                                   .frames()),
              expected);
    EXPECT_EQ(cpu_sequence(ScenarioBuilder()
                                   .repeat_for(100ms)
                                   .cpu(1ms)
                                   .frames_for(5s)
                                   .end_repeat()
                                   .cpu(3ms)
                                   .frames()),
              expected);
}

TEST(ScenarioEngineTest, TimedBlocksStartTheirClockAtTheirFirstFrame) {
    // Re-entering a timed block restarts it: both outer iterations play the
    // inner 100 ms in full.
    std::vector<double> expected;
    for (int i = 0; i < 2; ++i) {
        expected.insert(expected.end(), 10, 1);
        expected.insert(expected.end(), 10, 2);
    }
    ScenarioBuilder restarted;
    restarted.repeat(2, [](ScenarioBuilder& body) {
        body.cpu(1ms).frames_for(100ms);
        body.repeat_for(100ms, [](ScenarioBuilder& inner) { inner.cpu(2ms).frames(); });
    });
    EXPECT_EQ(cpu_sequence(restarted), expected);

    // A timed block whose first frame comes late still gets its full time.
    const Scenario scenario =
            built(ScenarioBuilder().repeat_for(100ms).cpu(1ms).frames().end_repeat());
    ScenarioEngine engine;
    engine.start(scenario);
    uint64_t now_ns = kFakeStartNs + 5000ULL * kFakeFrameNs;
    size_t count = 0;
    while (!engine.next_frame(now_ns).finished) {
        ++count;
        now_ns += kFakeFrameNs;
    }
    EXPECT_EQ(count, 10u);
}

ScenarioOp make_op(ScenarioOpKind kind, int64_t operand = 0) {
    ScenarioOp result;
    result.kind = kind;
    result.operand = operand;
    return result;
}

// A one-step scenario built from ops the builder would not emit; repeat ops
// get their partners by nesting.
Scenario scenario_from_ops(std::vector<ScenarioOp> ops) {
    std::vector<int32_t> open;
    for (size_t i = 0; i < ops.size(); ++i) {
        const int32_t index = static_cast<int32_t>(i);
        if (ops[i].kind == ScenarioOpKind::kRepeat || ops[i].kind == ScenarioOpKind::kRepeatFor) {
            open.push_back(index);
        } else if (ops[i].kind == ScenarioOpKind::kRepeatEnd) {
            ops[i].partner = open.back();
            ops[open.back()].partner = index;
            open.pop_back();
        }
    }
    return Scenario("", "", {"1."}, std::move(ops));
}

TEST(ScenarioEngineTest, RepeatBodiesWithoutFramesDoNotSpin) {
    // The builder rejects these bodies; the engine still must not loop on
    // them, a timed one in particular would never expire.
    using K = ScenarioOpKind;
    const Scenario counted = scenario_from_ops({make_op(K::kStepBegin, 0), make_op(K::kRepeat, 3),
                                                make_op(K::kSetCpu, 5 * 1000000LL),
                                                make_op(K::kRepeatEnd), make_op(K::kFrames, 1)});
    EXPECT_EQ(cpu_sequence(counted, 10), (std::vector<double>{5}));

    const Scenario timed = scenario_from_ops(
            {make_op(K::kStepBegin, 0), make_op(K::kRepeatFor, 5 * 1000000000LL),
             make_op(K::kSetCpu, 6 * 1000000LL), make_op(K::kRepeatEnd), make_op(K::kFrames, 1)});
    EXPECT_EQ(cpu_sequence(timed, 10), (std::vector<double>{6}));

    const Scenario zero_frames = scenario_from_ops(
            {make_op(K::kStepBegin, 0), make_op(K::kRepeat, 2), make_op(K::kFrames, 0),
             make_op(K::kRepeatEnd), make_op(K::kSetCpu, 7 * 1000000LL), make_op(K::kFrames, 1)});
    EXPECT_EQ(cpu_sequence(zero_frames, 10), (std::vector<double>{7}));
}

TEST(ScenarioEngineTest, StepBoundariesAndCompletion) {
    ScenarioBuilder builder;
    builder.step("a").cpu(1ms).frames(2);
    builder.step("b").cpu(2ms).frames(2);
    const Scenario scenario = built(builder);
    ScenarioEngine engine;
    engine.start(scenario);
    uint64_t now_ns = kFakeStartNs;
    const int expected_steps[] = {0, 0, 1, 1};
    const bool expected_begins[] = {true, false, true, false};
    int current_step_index = -1;
    for (int i = 0; i < 4; ++i, now_ns += kFakeFrameNs) {
        const ScenarioEngine::Frame frame = engine.next_frame(now_ns);
        EXPECT_FALSE(frame.finished) << i;
        EXPECT_EQ(frame.step_index, expected_steps[i]) << i;
        EXPECT_EQ(frame.step_index != current_step_index, expected_begins[i]) << i;
        current_step_index = frame.step_index;
        EXPECT_FALSE(engine.finished());
    }
    const ScenarioEngine::Frame last = engine.next_frame(now_ns);
    EXPECT_TRUE(last.finished);
    EXPECT_EQ(last.step_index, 1);
    EXPECT_TRUE(engine.finished());
    EXPECT_TRUE(engine.next_frame(now_ns + kFakeFrameNs).finished);

    ScenarioEngine idle;
    EXPECT_TRUE(idle.next_frame(kFakeStartNs).finished);
    idle.start(dvfs::Scenario());
    EXPECT_TRUE(idle.next_frame(kFakeStartNs).finished);
}

TEST(ScenarioEngineTest, ActionsFireOnceWithTheNextFrame) {
    ScenarioBuilder builder;
    builder.step("a").reset_frame_pacing().boost(500ms).cpu(1ms).frames(3);
    builder.step("b").frames().boost(250ms).frames();
    const Scenario scenario = built(builder);
    EXPECT_TRUE(scenario.needs_boost());
    const std::vector<ScenarioEngine::Frame> frames = play(scenario);
    ASSERT_EQ(frames.size(), 5u);
    const bool expected_reset[] = {true, false, false, false, false};
    const int expected_boost_ms[] = {500, 0, 0, 0, 250};
    for (size_t i = 0; i < frames.size(); ++i) {
        EXPECT_EQ(frames[i].reset_frame_pacing, expected_reset[i]) << i;
        EXPECT_EQ(frames[i].boost_ms, expected_boost_ms[i]) << i;
    }
    EXPECT_FALSE(built(ScenarioBuilder().reset_frame_pacing().frames()).needs_boost());
}

TEST(ScenarioEngineTest, NestedTimedBlocksExpireIndependently) {
    // The inner 1 s block restarts with every outer iteration; the outer
    // 3 s block ends everything at 3 s, the inner block's third run three
    // frames short, and the scenario goes on after it.
    ScenarioBuilder builder;
    builder.repeat_for(3s, [](ScenarioBuilder& body) {
        body.cpu(1ms).frames();
        body.repeat_for(1s, [](ScenarioBuilder& inner) { inner.cpu(2ms).frames(); });
    });
    builder.cpu(9ms).frames();
    const std::vector<ScenarioEngine::Frame> frames = play(builder);
    std::vector<double> expected;
    for (int inner_frames : {100, 100, 97}) {
        expected.push_back(1);
        expected.insert(expected.end(), inner_frames, 2);
    }
    expected.push_back(9);
    EXPECT_EQ(cpu_sequence(frames), expected);
}

TEST(ScenarioEngineTest, OuterTimeLimitEndsInnerTimedBlockAndFrames) {
    // 2 s hold 200 frames: the frames(100), then the inner 5 s block, which has
    // 4 s of its own time left, end with the outer block; so does a frames(N)
    // in flight inside the inner block.
    std::vector<double> expected(100, 1);
    expected.insert(expected.end(), 100, 2);
    expected.push_back(3);
    ScenarioBuilder single_inner;
    single_inner.repeat_for(2s, [](ScenarioBuilder& body) {
        body.cpu(1ms).frames(100);
        body.repeat_for(5s, [](ScenarioBuilder& inner) { inner.cpu(2ms).frames(); });
    });
    single_inner.cpu(3ms).frames();
    EXPECT_EQ(cpu_sequence(play(single_inner)), expected);

    ScenarioBuilder counted_inner;
    counted_inner.repeat_for(2s, [](ScenarioBuilder& body) {
        body.cpu(1ms).frames(100);
        body.repeat_for(5s, [](ScenarioBuilder& inner) { inner.cpu(2ms).frames(1000); });
    });
    counted_inner.cpu(3ms).frames();
    EXPECT_EQ(cpu_sequence(play(counted_inner)), expected);
}

TEST(ScenarioEngineTest, TimedBlockInsideCountBlockRestartsItsClock) {
    // Each of the three iterations plays the inner second in full.
    std::vector<double> expected(300, 1);
    expected.push_back(2);
    ScenarioBuilder builder;
    builder.repeat(3, [](ScenarioBuilder& body) {
        body.repeat_for(1s, [](ScenarioBuilder& inner) { inner.cpu(1ms).frames(); });
    });
    builder.cpu(2ms).frames();
    const std::vector<ScenarioEngine::Frame> frames = play(builder);
    EXPECT_EQ(cpu_sequence(frames), expected);
    EXPECT_EQ(step_starts(frames), (std::vector<size_t>{0}));
}

TEST(ScenarioEngineTest, ActionsInsideRepeatFireEveryIteration) {
    // An action in a repeat body fires with the body's next frame on every
    // iteration; one after the body's last frame fires with the next
    // iteration's first frame, and not once the block has expired.
    ScenarioBuilder builder;
    builder.repeat(3, [](ScenarioBuilder& body) { body.boost(500ms).cpu(1ms).frames(2); });
    builder.repeat_for(50ms, [](ScenarioBuilder& body) { body.frames().reset_frame_pacing(); });
    const Scenario program = built(builder);
    EXPECT_TRUE(program.needs_boost());
    const std::vector<ScenarioEngine::Frame> frames = play(program);
    ASSERT_EQ(frames.size(), 11u);
    EXPECT_EQ(boost_frames(frames), (std::vector<size_t>{0, 2, 4}));
    EXPECT_EQ(reset_pacing_frames(frames), (std::vector<size_t>{7, 8, 9, 10}));
}

TEST(ScenarioEngineTest, ActionsMidStepFireOnTheRightFrame) {
    // An action fires with the frame that follows it, wherever it stands
    // in the step, and with no other.
    std::vector<ScenarioEngine::Frame> frames =
            play(ScenarioBuilder().cpu(1ms).frames(2).reset_frame_pacing().frames(2));
    ASSERT_EQ(frames.size(), 4u);
    EXPECT_EQ(reset_pacing_frames(frames), (std::vector<size_t>{2}));
    EXPECT_TRUE(boost_frames(frames).empty());

    ScenarioBuilder two_steps;
    two_steps.step("a").frames(2).boost(500ms).reset_frame_pacing().frames();
    two_steps.step("b").frames();
    frames = play(two_steps);
    ASSERT_EQ(frames.size(), 4u);
    EXPECT_EQ(boost_frames(frames), (std::vector<size_t>{2}));
    EXPECT_EQ(reset_pacing_frames(frames), (std::vector<size_t>{2}));
    EXPECT_EQ(step_starts(frames), (std::vector<size_t>{0, 3}));
}

TEST(ScenarioEngineTest, StartResetsStateAndFinishedStaysFinished) {
    ScenarioBuilder builder;
    builder.step("a").cpu(1ms).frames(2).repeat(2, [](ScenarioBuilder& body) {
        body.boost(500ms).cpu(2ms).frames();
        body.repeat_for(30ms, [](ScenarioBuilder& inner) { inner.cpu(3ms).frames(); });
    });
    builder.step("b").reset_frame_pacing().cpu(4ms).frames_for(20ms);
    const Scenario program = built(builder);
    const std::vector<ScenarioEngine::Frame> reference = play(program);
    EXPECT_EQ(cpu_sequence(reference), (std::vector<double>{1, 1, 2, 3, 3, 3, 2, 3, 3, 3, 4, 4}));
    EXPECT_EQ(boost_frames(reference), (std::vector<size_t>{2, 6}));
    EXPECT_EQ(reset_pacing_frames(reference), (std::vector<size_t>{10}));

    // Starting again part-way through replays from the top, the time blocks
    // counting from their new first frames, whatever the clock reads.
    ScenarioEngine engine;
    engine.start(program);
    for (uint64_t i = 0; i < 5; ++i) engine.next_frame(kFakeStartNs + i * kFakeFrameNs);
    engine.start(program);
    EXPECT_FALSE(engine.finished());
    expect_same_frames(play_started(&engine, kFakeStartNs + 98765ULL * kFakeFrameNs), reference);
    EXPECT_TRUE(engine.finished());

    // Once finished, the engine stays finished and plays nothing more.
    for (uint64_t i = 0; i < 3; ++i) {
        const ScenarioEngine::Frame frame = engine.next_frame(kFakeStartNs + i * kFakeFrameNs);
        EXPECT_TRUE(frame.finished);
        EXPECT_EQ(frame.step_index, 1);
        EXPECT_TRUE(engine.finished());
    }

    // A finished engine starts over.
    engine.start(program);
    EXPECT_FALSE(engine.finished());
    expect_same_frames(play_started(&engine, kFakeStartNs), reference);
}

TEST(ScenarioEngineTest, EmptyProgramFinishesImmediately) {
    ScenarioBuilder empty_builder("X", "d");
    const Scenario program = built(empty_builder);
    EXPECT_EQ(program.title(), "X");
    EXPECT_TRUE(program.step_names().empty());
    EXPECT_TRUE(program.ops().empty());
    ScenarioEngine engine;
    engine.start(program);
    const ScenarioEngine::Frame frame = engine.next_frame(kFakeStartNs);
    EXPECT_TRUE(frame.finished);
    EXPECT_EQ(frame.step_index, -1);
    EXPECT_TRUE(engine.finished());
}

} // namespace
} // namespace dvfs
