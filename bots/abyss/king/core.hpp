#pragma once
// King end-game library core: protocol, crown and mode, sigma, hunger, feeding. Header-only, namespace bc::king, no
// dependency on World or the search code (plain input structs). The adapter in v10k.hpp fills them from World; unit
// tests and simulators can include this file directly (the judge compiles every .cpp under a bot folder, so no test
// may live here). The library is compiled into the final build but never runs (V10K_R0 = 501 > 500 rounds).
//
// Per-turn order for one process:
//   bb.beginTurn(r); bb.receive(inbox, enemy ids in vision)
//   crown.beginTurn(bb, CrownInput)                      -> role (KING / HEIR / BACKUP / none), tombstones
//   king:     KingStats, SigmaState::update, Hunger::update, ModeMachine::update -> sigma, h, mode, busy
//   non-king: kingViewOf(bb) -> feedEligible / feedDeathD5 / feedDeathD5p / factorySplit / cappedCull
//   ... action ...
//   crown.afterAction(postLen); predictRay x4; planRays(bb, preds, PlanInput{..., crown.kingMsg(...)})

#include "crown.hpp"
#include "feed.hpp"
#include "hunger.hpp"
#include "proto.hpp"
#include "sigma.hpp"
