/*
 *	icache Prefetch event builder module implementation
 *
 *	This file is part of OTAWA
 *	Copyright (c) 2017, IRIT UPS.
 *
 *	OTAWA is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	OTAWA is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with OTAWA; if not, write to the Free Software
 *	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <otawa/proc/BBProcessor.h>
#include <otawa/cache/features.h>
#include <otawa/cache/cat2/CAT2Builder.h>
#include <otawa/ilp/Constraint.h>
#include <otawa/events/features.h>
#include <otawa/etime/features.h>
#include <otawa/icache/features.h>
#include <otawa/hard/Memory.h>

namespace otawa {
namespace xilinx {
using namespace icache;

class ICacheEvent: public otawa::Event {
public:
	
	ICacheEvent(Inst *inst, 
                ot::time cost,
                LBlock *lb) : Event(inst), _lb(lb), _cost(cost) { }

	cstring name() const override { return "IC"; }
	string detail() const override {
		StringBuffer buf;
		buf << "@ " << _lb->address() << " - " << CATEGORY(_lb);
		// cout << buf.copyString()<< endl;
		return buf.toString();
	}

	kind_t kind() const override { return FETCH; }
	ot::time cost() const override { return _cost; }
	occurrence_t occurrence() const override {
        switch(CATEGORY(_lb)) {
            case ALWAYS_HIT:		return NEVER;
            case FIRST_HIT:			return SOMETIMES;
            case FIRST_MISS:		return SOMETIMES;
            case ALWAYS_MISS:		return ALWAYS;
            case NOT_CLASSIFIED:	return ALWAYS;
            default:				return ALWAYS;
		}
    }
	type_t type() const override { return LOCAL; }

	bool isEstimating(bool on) const override {
		if(on)
			return CATEGORY(_lb) != NOT_CLASSIFIED;
		else
			return false;
	}
	
	void estimate(ilp::Constraint *cons, bool on) const override {
		if(on)
			cons->addLeft(1, otawa::MISS_VAR(_lb));
	}

    int weight() const override {
		switch(CATEGORY(_lb)) {
            case ALWAYS_HIT:
                return 0;
            case FIRST_HIT:
            case ALWAYS_MISS:
            case NOT_CLASSIFIED:	
                return WEIGHT(_lb->bb());
            case FIRST_MISS:		
                {
                    Block* parent = otawa::ENCLOSING_LOOP_HEADER(_lb);
                    if(!parent)
                        return 1;
                    else
                        return WEIGHT(parent);
                }
                break;
            default:
                return ALWAYS;
		}
        return 0;
	}

private:
	LBlock* _lb;
	ot::time _cost;
};


class PrefetchEventBuilder: public BBProcessor {
public:
	static p::declare reg;
    const hard::Memory* mem;
	PrefetchEventBuilder(void): BBProcessor(reg) { }

protected:

    ot::time getAccessCost(const Address& inst_addr) {
        const hard::Bank* bank = mem->get(inst_addr);
        if(bank) {
            return bank->readLatency();
        } else
            return mem->worstAccessTime();
    }

    void setup(WorkSpace* ws) override {
        BBProcessor::setup(ws);
        mem = hard::MEMORY_FEATURE.get(ws);
    }

	virtual void processBB(WorkSpace* ws, CFG* cfg, Block* b) {

		if(!b->isBasic())
			return;

		BasicBlock* bb = b->toBasic();

		if(bb->isEnd())
			return;
        // Get the L-Blocks of the BB
		AllocArray<LBlock* >* l_blocks = BB_LBLOCKS(bb);
        
		for(int i = 0; i < l_blocks->size(); i++) {
			// get LBlock and its instruction access category
			LBlock* block = l_blocks->get(i);

			switch(cache::CATEGORY(block)) {
                case cache::ALWAYS_HIT:
                    break;
                case cache::FIRST_HIT:
                case cache::INVALID_CATEGORY:
                case cache::ALWAYS_MISS:
                case cache::FIRST_MISS:
                case cache::NOT_CLASSIFIED:
                    etime::EVENT(bb).add(new ICacheEvent(block->instruction(), getAccessCost(block->instruction()->address()), block));
                    break;
			}
		}
	}
};

p::declare PrefetchEventBuilder::reg = p::init("otawa::xilinx::PrefetchEventBuilder", Version(1, 0, 0))
	.make<PrefetchEventBuilder>()
	.require(otawa::ICACHE_CATEGORY2_FEATURE);

} // namespace xilinx
} // namespace otawa
