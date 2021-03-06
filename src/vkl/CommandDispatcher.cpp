#include <CommandDispatcher.h>

#include <Device.h>
#include <SwapChain.h>
#include <RenderObject.h>
#include <RenderPass.h>

#include <future>
#include <iostream>
#include <array>

namespace vkl
{
	class CommandThread
	{
	public:


		CommandThread(const Device& device, const SwapChain& swapChain)
		{
			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = swapChain.graphicsFamilyQueueIndex();
			poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			if (vkCreateCommandPool(device.handle(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) {
				throw std::runtime_error("Error");
			}

			_commandBuffers.resize(swapChain.framesInFlight());

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = _commandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
			allocInfo.commandBufferCount = (uint32_t)_commandBuffers.size();

			if (vkAllocateCommandBuffers(device.handle(), &allocInfo, _commandBuffers.data()) != VK_SUCCESS) {
				throw std::runtime_error("Error");
			}

		}
		CommandThread() = delete;
		~CommandThread() = default;
		CommandThread(const CommandThread&) = delete;
		CommandThread(CommandThread&&) noexcept = default;
		CommandThread& operator=(CommandThread&&) noexcept = default;
		CommandThread& operator=(const CommandThread&) = delete;

		std::future<VkCommandBuffer> processObjects(std::span< std::shared_ptr<RenderObject>> objects, const PipelineManager& pipelines, const RenderPass& pass, const SwapChain& swapChain, VkFramebuffer frameBuffer, const VkExtent2D& extent)
		{
			return std::async( [&]() {


				VkCommandBufferInheritanceInfo inherit{};
				inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
				inherit.framebuffer = frameBuffer;
				inherit.renderPass = pass.handle();
				inherit.subpass = 0;//todo

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				beginInfo.pInheritanceInfo = &inherit;

				if (vkBeginCommandBuffer(_commandBuffers[swapChain.frame()], &beginInfo) != VK_SUCCESS) {
					throw std::runtime_error("Error");
				}

				VkViewport viewport{};
				viewport.height = (float)extent.height;
				viewport.width = (float)extent.width;
				viewport.minDepth = 0.0;
				viewport.maxDepth = 1.0;
				vkCmdSetViewport(_commandBuffers[swapChain.frame()], 0, 1, &viewport);


				for (auto&& object : objects)
				{
					object->recordCommands(swapChain, pipelines, _commandBuffers[swapChain.frame()], extent);
				}

				if (vkEndCommandBuffer(_commandBuffers[swapChain.frame()]) != VK_SUCCESS) {
					throw std::runtime_error("Error");
				}

				return _commandBuffers[swapChain.frame()];
			});
		}

		VkCommandBuffer handle(size_t frame) const
		{
			return _commandBuffers[frame];
		}
	private:
		VkCommandPool _commandPool{ VK_NULL_HANDLE };
		std::vector<VkCommandBuffer> _commandBuffers;

		std::thread _thread;

		
	};


	CommandDispatcher::CommandDispatcher(const Device& device, const SwapChain& swapChain)
	{
		unsigned int threadCount = std::thread::hardware_concurrency();
		
		for (unsigned int i = 0; i < threadCount; ++i)
			_threads.emplace_back(std::move(std::make_unique<CommandThread>(device, swapChain)));


		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = swapChain.graphicsFamilyQueueIndex();
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		if (vkCreateCommandPool(device.handle(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) {
			throw std::runtime_error("Error");
		}
		_primaryBuffers.resize(swapChain.framesInFlight());

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = _commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)_primaryBuffers.size();

		if (vkAllocateCommandBuffers(device.handle(), &allocInfo, _primaryBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("Error");
		}

	}

	CommandDispatcher::CommandDispatcher(CommandDispatcher&&) noexcept = default;
	CommandDispatcher& CommandDispatcher::operator=(CommandDispatcher&&) noexcept = default;

	CommandDispatcher::~CommandDispatcher()
	{
	}
	void CommandDispatcher::processUnsortedObjects(std::span< std::shared_ptr<RenderObject>> objects, const PipelineManager& pipelines, const RenderPass& pass, const SwapChain& swapChain, VkFramebuffer frameBuffer, const VkExtent2D& extent)
	{
		//record commands
		size_t objectCount = objects.size();
		size_t objectsPerThread = objectCount / _threads.size() + 1;

		size_t offset = 0;
		size_t threadIndex = 0;
		std::vector<std::future<VkCommandBuffer>> futures;
		std::vector<VkCommandBuffer> buffers;

		while (offset < objects.size())
		{
			futures.push_back(_threads[threadIndex]->processObjects(objects.subspan(offset, objectsPerThread), pipelines, pass, swapChain, frameBuffer, extent));
			offset += objectsPerThread;
			++threadIndex;
		}
		for (auto&& future : futures)
			buffers.push_back(future.get());

		
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0;

		if (vkBeginCommandBuffer(_primaryBuffers[swapChain.frame()], &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("Error");
		}

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = pass.handle();
		renderPassInfo.framebuffer = frameBuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;

		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = pass.options().clearColor;
		clearValues[1].depthStencil = pass.options().clearDepthStencil;

		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(_primaryBuffers[swapChain.frame()], &renderPassInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		vkCmdExecuteCommands(_primaryBuffers[swapChain.frame()], (uint32_t)buffers.size(), buffers.data());

		vkCmdEndRenderPass(_primaryBuffers[swapChain.frame()]);

		if (vkEndCommandBuffer(_primaryBuffers[swapChain.frame()]) != VK_SUCCESS) {
			throw std::runtime_error("Error");
		}

	}
	VkCommandBuffer CommandDispatcher::primaryCommandBuffer(size_t frame) const
	{
		return _primaryBuffers[frame];
	}
}