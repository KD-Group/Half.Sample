#include <unordered_map>
#include <functional>
#include <string>
#include "commander.hpp"
#include "measure.hpp"
#include "../global/global.hpp"
#include "../sampler/sampler_factory.hpp"

namespace Commander {

    void simple_test() {
        bool success = true;
        Base::variable(success);
    }

    void set_sampler() {
        std::string sampler_name;
        std::cin >> sampler_name;

        Global::sampler = Sampler::SamplerFactory::get(sampler_name);
        if (Global::sampler.get()) {
            Base::variable(sampler_name);
        } else {
            Base::error(Error::SAMPLER_NOT_FOUND);
        }
    }

    void get_sampler() {
        std::string sampler_name;

        if (Global::sampler) {
            sampler_name = Global::sampler->name();
        }

        Base::variable(sampler_name);
    }

    void set_sampler_value() {
        std::string key;
        double value;
        std::cin >> key >> value;
        Global::sampler->set_value(key, value);
        Base::line(key, Global::sampler->get_value(key));
    }

    void get_sampler_value() {
        std::string key;
        std::cin >> key;
        Base::line(key, Global::sampler->get_value(key));
    }

    void exec() {
        std::unordered_map<std::string, std::function<void(void)> > mapper;

#define add_func_into_mapper(func, mapper) mapper[#func] = func
        add_func_into_mapper(simple_test, mapper);

        add_func_into_mapper(set_sampler, mapper);
        add_func_into_mapper(get_sampler, mapper);
        add_func_into_mapper(set_sampler_value, mapper);
        add_func_into_mapper(get_sampler_value, mapper);

        add_func_into_mapper(to_query, mapper);
        add_func_into_mapper(to_measure, mapper);
        add_func_into_mapper(is_measuring, mapper);
        add_func_into_mapper(to_dump, mapper);
        add_func_into_mapper(to_process, mapper);

        std::string command;
        while (std::cin >> command) {
            if (mapper.count(command)) {
                mapper[command]();
            } else {
                Base::error(Error::COMMAND_NOT_FOUND);
            }
            Base::end();
        }
    }

} // namespace Commander
