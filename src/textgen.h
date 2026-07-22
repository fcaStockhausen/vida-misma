#pragma once
// Procedural text generator for La Vida Misma.
// Lightweight context-free grammar (CFG) inspired by Tracery.
//
// Grammar format (C++ initialized):
//   generator.add("sabotage", {"{feeling}. {action}.", "I {action}. {feeling}."});
//   generator.add("feeling", {"Something broke", "The grey got heavy", "I stopped caring"});
//   generator.add("action", {"hit the machine", "tore at the belt", "pulled a cable out"});
//
// Expansion:
//   generator.generate("sabotage", context, rng) → "The grey got heavy. I tore at the belt."
//
// Context: maps symbol names to pre-selected strings, so callers can
// inject archetype/stress-specific phrases before expansion.

#include <string>
#include <vector>
#include <map>
#include <random>
#include <sstream>

class TextGen {
public:
    using Alternatives = std::vector<std::string>;

    // Add or replace a rule
    void add(const std::string& symbol, const Alternatives& opts) {
        rules_[symbol] = opts;
    }

    // Set a context variable (resolved before expansion)
    void set_context(const std::string& key, const std::string& val) {
        context_[key] = val;
    }

    // Clear context between generations
    void clear_context() { context_.clear(); }

    // Generate by expanding a symbol recursively
    std::string generate(const std::string& symbol, std::mt19937& rng) {
        // First: check if this is a grammar rule name
        auto it = rules_.find(symbol);
        if (it != rules_.end() && !it->second.empty()) {
            const auto& opts = it->second;
            std::uniform_int_distribution<int> dist(0, (int)opts.size() - 1);
            std::string result;
            expand(opts[dist(rng)], result, rng);
            return result;
        }
        // Not a rule: expand as template (may contain {symbols})
        std::string result;
        expand(symbol, result, rng);
        return result;
    }

private:
    std::map<std::string, Alternatives> rules_;
    std::map<std::string, std::string> context_;

    void expand(const std::string& input, std::string& out, std::mt19937& rng) {
        size_t pos = 0;
        while (pos < input.size()) {
            // Find next '{'
            size_t open = input.find('{', pos);
            if (open == std::string::npos) {
                out += input.substr(pos);
                return;
            }
            // Text before brace
            out += input.substr(pos, open - pos);
            // Find closing '}'
            size_t close = input.find('}', open);
            if (close == std::string::npos) {
                out += input.substr(open);
                return;
            }
            // Extract symbol name
            std::string sym = input.substr(open + 1, close - open - 1);
            // Check context first
            auto ctx_it = context_.find(sym);
            if (ctx_it != context_.end()) {
                // Context value may itself contain braces — expand recursively
                expand(ctx_it->second, out, rng);
            } else {
                // Grammar rule
                auto rule_it = rules_.find(sym);
                if (rule_it != rules_.end() && !rule_it->second.empty()) {
                    const auto& opts = rule_it->second;
                    std::uniform_int_distribution<int> dist(0, (int)opts.size() - 1);
                    const std::string& chosen = opts[dist(rng)];
                    expand(chosen, out, rng);
                } else {
                    // Unknown symbol — leave as-is
                    out += "{" + sym + "}";
                }
            }
            pos = close + 1;
        }
    }
};

// ============================================================
// Factory: build the grammar for La Vida Misma
// ============================================================

inline TextGen make_narrative_grammar() {
    TextGen g;

    // ---- SPAWNED ----
    g.add("spawned", {
        "{arch_feeling}. {arch_arrival}.",
        "{arch_arrival}. {arch_feeling}.",
        "{arch_feeling}.",
    });
    g.add("arch_arrival", {
        "I looked around", "I found a spot", "I started walking",
        "I kept my head down", "I found the others",
    });

    // ---- DEATH: STARVATION ----
    g.add("died_starvation", {
        "{hunger_feeling}. {dying_thought}.",
        "{dying_thought}. {hunger_feeling}.",
    });
    g.add("hunger_feeling", {
        "The hunger ate through everything",
        "My stomach stopped hurting a while ago",
        "I couldn't remember the last time I ate",
        "Everything tasted like dust",
        "My hands shook too much to hold anything",
    });
    g.add("dying_thought", {
        "The machine didn't notice",
        "Nobody brought food",
        "I should have eaten instead of working",
        "I should have eaten instead of creating",
        "At least the hunger is quiet now",
    });

    // ---- DEATH: EXHAUSTION ----
    g.add("died_exhaustion", {
        "I just needed to close my eyes. Just for a moment.",
        "My legs gave out. I didn't get up.",
        "The floor felt warm. I stopped fighting it.",
        "I was so tired I forgot what tired meant.",
    });

    // ---- DEATH: BREAKDOWN ----
    g.add("died_breakdown", {
        "Something inside me broke and I couldn't find all the pieces.",
        "I stopped recognizing the walls. Then I stopped recognizing my hands.",
        "The noise in my head got louder than the machines.",
        "I couldn't hold it together anymore. I don't know what 'it' was.",
    });

    // ---- DEATH: SUICIDE ----
    g.add("died_suicide", {
        "This machine ate everything I was. I chose to stop letting it.",
        "I walked into the machine. It didn't slow down. Neither did I.",
        "I couldn't make it stop. So I stopped myself.",
        "There was nothing left of me the factory hadn't taken.",
    });

    // ---- SABOTAGE ----
    g.add("sabotage", {
        "{sab_feeling}. {sab_action}.",
        "{sab_action}. {sab_feeling}.",
        "{sab_feeling}. So {sab_action_lower}.",
    });
    g.add("sab_feeling", {
        "Something in me snapped",
        "I couldn't watch it run for one more second",
        "The hum got inside my head",
        "I wanted it to feel what we feel",
        "My hands moved before I could think",
    });
    g.add("sab_action", {
        "I hit the machine",
        "I tore at the belt",
        "I pulled a cable out",
        "I slammed my fist into the housing",
        "I kicked the conveyor until it stopped",
    });
    g.add("sab_action_lower", {
        "I broke what I could reach",
        "I pulled at anything that looked important",
        "I hit it until my hands hurt",
        "I yanked the belt off its track",
    });

    // ---- OPTIONAL INTERPRETATION OF A POST-SABOTAGE PAUSE ----
    g.add("redemption", {
        "{red_thought}. {red_resolution}.",
        "{red_resolution}. {red_thought}.",
    });
    g.add("red_thought", {
        "I looked at the others and saw myself",
        "I've been breaking what I should be building",
        "They're suffering too and I made it worse",
        "I forgot why I came here",
        "The machine isn't the others",
    });
    g.add("red_resolution", {
        "I have to help them now",
        "I'll use my hands for something else",
        "It's time to give something back",
        "I won't break anything else",
    });

    // ---- BREAKDOWN ----
    g.add("breakdown", {
        "I can't do this anymore. Any of it.",
        "The walls started breathing. I think I did too.",
        "Everything went grey and stayed that way.",
        "I forgot what I was doing mid-sentence.",
        "My hands wouldn't stop shaking.",
    });

    // ---- ARTIFACT CREATED ----
    g.add("artifact", {
        "{create_impulse}. {create_result}.",
        "{create_result}. {create_impulse}.",
        "{create_impulse}.",
    });
    g.add("create_impulse", {
        "I needed to make something that wasn't the machine",
        "My hands were shaking and this is what came out",
        "I don't know why I made it",
        "There was a gap between the noise and this is what filled it",
        "I wanted to prove something existed outside the quota",
    });
    g.add("create_result", {
        "It doesn't feed anyone",
        "It won't help us meet quota",
        "Nobody will see it",
        "It's mine",
        "It's ugly but it's real",
        "It doesn't do anything",
    });

    // ---- BUILD MACHINE ----
    g.add("built", {
        "{build_feeling}. {build_result}.",
        "{build_result}. {build_feeling}.",
    });
    g.add("build_feeling", {
        "I worked until my hands hurt",
        "Another thing for the machine",
        "I know what this is for",
        "It won't last but I built it right",
        "The factory grew a little today",
    });
    g.add("build_result", {
        "It runs",
        "It's done",
        "Another piece in place",
        "It'll hold for now",
    });

    // ---- WORK COMPLETED ----
    g.add("work", {
        "{work_feeling}. {work_close}.",
        "{work_close}. {work_feeling}.",
        "{work_feeling}.",
    });
    g.add("work_feeling", {
        "The hours blur",
        "My hands know the motions now",
        "The quota doesn't care how I feel",
        "Another shift dissolves",
        "I lost track of how long I've been here",
    });
    g.add("work_close", {
        "Shift done",
        "The machine is satisfied for now",
        "I wipe my hands and move on",
        "Another day feeds the belt",
    });

    // ---- FOOD SHARED ----
    g.add("shared", {
        "{share_feeling}. {share_action}.",
        "{share_action}. {share_feeling}.",
    });
    g.add("share_feeling", {
        "I know what it's like to be empty",
        "They looked worse than me",
        "We're all in this",
        "It's not much but it's something",
    });
    g.add("share_action", {
        "I gave them what I could",
        "I split my portion",
        "I handed it over without thinking",
        "I pushed my food toward them",
    });

    // ---- GATHERED ----
    g.add("gathered", {
        "{gather_find}. {gather_close}.",
        "{gather_close}. {gather_find}.",
    });
    g.add("gather_find", {
        "There was something useful in the scrap",
        "I found enough to be worth the walk",
        "The pile had something left in it",
        "My hands are raw but I got something",
    });
    g.add("gather_close", {
        "Into the inventory",
        "Every bit counts",
        "The machine needs it more than the ground does",
        "One more piece for something",
    });

    // ---- HIDDEN SPACE FOUND ----
    g.add("hidden_space", {
        "{hidden_discovery}. {hidden_feeling}.",
        "{hidden_feeling}. {hidden_discovery}.",
    });
    g.add("hidden_discovery", {
        "There's a corner here nobody knows about",
        "I found a gap in the walls",
        "There's a spot where the machine can't see",
        "I squeezed through and found empty space",
    });
    g.add("hidden_feeling", {
        "It's quiet here",
        "For a moment the hum fades",
        "Nobody will find me here",
        "This is mine. This one thing.",
    });

    // ---- COMMUNITY DETECTED ----
    g.add("community_detected", {
        "We found each other. We stand together now.",
        "A group of us — finally, people I trust.",
        "We talked and realized: we're the same.",
        "There are others who see it my way. We hold.",
    });

    // ---- TRUST MILESTONE ----
    g.add("trust", {
        "I think I can trust them now. Maybe.",
        "They haven't let me down yet.",
        "I caught myself relying on them. It didn't feel wrong.",
        "Trust is expensive here. I think they're worth it.",
    });

    // ---- MACHINE BROKE ----
    g.add("machine_broke", {
        "Another one grinds to a halt.",
        "The belt stopped. Something inside gave out.",
        "I heard the noise change. Then it stopped.",
        "One less machine running. We're losing ground.",
    });

    // ---- FACTORY CRISIS ----
    g.add("crisis", {
        "Everything is falling apart.",
        "The walls feel closer. The quota feels heavier.",
        "I can hear the machines dying one by one.",
        "The factory is eating itself.",
    });

    // ---- CONTEXT-dependent phrases (set by caller) ----
    // {arch_feeling} is set by the caller based on archetype:
    //   set_context("arch_feeling", "...") before generate()

    return g;
}
