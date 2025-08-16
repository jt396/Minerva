#include "vk_engine.h"

int main() {
	mnv::VulkanEngine engine;

	engine.init();
		engine.run();
	engine.cleanup();

	return 0;
}
