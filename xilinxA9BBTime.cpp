/*
 *	xilinx_A9 module implementation
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

#include <otawa/events/StandardEventBuilder.h>
#include <otawa/etime/EdgeTimeBuilder.h>
#include <otawa/prop/DynIdentifier.h>
#include <otawa/dcache/features.h>
#include <elm/sys/Path.h>
#include <otawa/loader/arm.h>
#include <elm/io/FileOutput.h>
#include <elm/data/Vector.h>

namespace otawa { namespace xilinx {
    using namespace elm::io;
    extern p::id<bool> WRITE_LOG;

    typedef enum {
        FE    = 0,
        IQ    = 1,
        DE    = 2,
        REGR  = 3,
        EXE   = 4,
        WB    = 5,
        CNT   = 6
    } pipeline_stage_t;
    
    class ExeGraph: public etime::EdgeTimeGraph {
	public:
		
		ExeGraph(WorkSpace* ws,
                 ParExeProc* proc, 
                 Vector<Resource* >* hw_resources, 
				 ParExeSequence* seq,
                 const PropList& props,
                 FileOutput* out, 
                 elm::Vector<Address>* unknown_inst_address) : etime::EdgeTimeGraph(ws, proc, hw_resources, seq, props), 
                                                                exec_f(0), exec_alu_mul(0), exec_ldst(0), _out(out), 
                                                                _unknown_inst_address(unknown_inst_address) {
			
			// Try to find arm loader with arm information
			DynIdentifier<arm::Info* > id("otawa::arm::Info::ID");
			info = id(_ws->process());
			if (!info)
				throw Exception("ARM loader with otawa::arm::INFO is required !");
			// Get memory configuration
			mem = hard::MEMORY_FEATURE.get(ws);
			ASSERTP(mem, "Memory feature not found");
		}

        private:
            otawa::arm::Info* info;
            const hard::Memory* mem;
            ParExeStage* stage[CNT];
            ParExePipeline *exec_f, *exec_alu_mul, *exec_ldst;
            FileOutput* _out = nullptr;
            elm::Vector<Address>* _unknown_inst_address = nullptr;
    };

    class BBTimerXilinxA9: public etime::EdgeTimeBuilder {
	public:
		static p::declare reg;
		BBTimerXilinxA9(void): etime::EdgeTimeBuilder(reg) { }

	protected:
		virtual void configure(const PropList& props) {
			etime::EdgeTimeBuilder::configure(props);
			// write_log = WRITE_LOG(props);
			_props = props;
		}
		void setup(WorkSpace* ws) override {
			etime::EdgeTimeBuilder::setup(ws);
			const hard::CacheConfiguration* cache_config = hard::CACHE_CONFIGURATION_FEATURE.get(ws);
			if (!cache_config)
				throw ProcessorException(*this, "no cache");
			dcache = cache_config->dataCache();
			if (!dcache)
				throw ProcessorException(*this, "no data cache");
			icache = cache_config->instCache();
			if (!icache)
				throw ProcessorException(*this, "no instruction cache");
			if (dcache == icache)
				throw ProcessorException(*this, "unified L1 cache not supported");
			if (write_log) {
				sys::Path log_file_path = sys::Path(ws->process()->program()->name() + ".log");
				bool write_header = (log_file_path.exists()) ? false : true;
				log_stream = new FileOutput(log_file_path, true);
				if (write_header)
					*log_stream << "########################################################" << endl
								<< "# Static analysis on " << ws->process()->program()->name() << endl
								<< "# Overestimated instructions" << endl
								<< "# Address (hex); Instruction" << endl
								<< "########################################################" << endl;
				else
					*log_stream << endl; // sep
				
				unknown_inst_address = new elm::Vector<Address>();
			}
		}

		etime::EdgeTimeGraph* make(ParExeSequence* seq) override {
			ExeGraph* graph = new ExeGraph(workspace(), _microprocessor, ressources(), seq, _props, log_stream, unknown_inst_address);
			graph->build();
			return graph;
		}


		virtual void clean(ParExeGraph* graph) {
			log_stream->flush();
			delete graph;
		}
	private:
		PropList _props;
		const hard::Cache *dcache, *icache;
		hard::Memory* mem;
		FileOutput* log_stream = nullptr;
		bool write_log = 0;
		elm::Vector<Address>* unknown_inst_address = nullptr;
	};

	

	p::declare BBTimerXilinxA9::reg = p::init("otawa::xilinx::BBTimerXilinxA9", Version(1, 0, 0))
										.extend<etime::EdgeTimeBuilder>()
										.require(otawa::hard::CACHE_CONFIGURATION_FEATURE)
										.maker<BBTimerXilinxA9>();
	


} // namespace xilinx
} // namespace otawa

