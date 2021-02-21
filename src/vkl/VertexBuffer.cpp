#include <VertexBuffer.h>

#include <Device.h>
#include <SwapChain.h>

namespace vkl
{
    VertexBuffer::VertexBuffer(const Device& device, const SwapChain& swapChain, uint32_t binding) : _binding(binding)
    {
        _buffers.resize(swapChain.framesInFlight());
    }

    void VertexBuffer::setData(void* data, size_t elementSize, size_t count)
    {
        _data = data;
        _elementSize = elementSize;
        _oldCount = _count;
        _count = count;
        _dirty = std::numeric_limits<int>::max();
    }

    void VertexBuffer::update(const Device& device, const SwapChain& swapChain)
    {
        if (_dirty == swapChain.frame())
            _dirty = -1;

        if (_dirty == -1)
            return;

        if (_dirty == std::numeric_limits<int>::max())
            _dirty = (int)swapChain.frame();

        auto& current = _buffers[swapChain.frame()];

        if (_oldCount != _count && isValid(swapChain.frame()))
        {
            //destroy buffer
            if (current._buffer && current._memory)
            {
                vmaDestroyBuffer(device.allocatorHandle(), current._buffer, current._memory);
                current._memory = nullptr;
                current._buffer = VK_NULL_HANDLE;
                current._mapped = nullptr;
            }
        }

        if (_count == 0)
            return;


        if (!isValid(swapChain.frame()))
        {
            //create buffer
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = _elementSize * _count;
            bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo createAllocation{};
            createAllocation.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            createAllocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            VmaAllocationInfo info{};
            vmaCreateBuffer(device.allocatorHandle(), &bufferInfo, &createAllocation, &current._buffer, &current._memory, &info);

            current._mapped = info.pMappedData;
        }

        //populate buffer
        if (current._mapped && _data && _count)
            memcpy(current._mapped, _data, _elementSize * _count);

    }

    void* VertexBuffer::data() const
    {
        return _data;
    }

    size_t VertexBuffer::elementSize() const
    {
        return _elementSize;
    }

    size_t VertexBuffer::count() const
    {
        return _count;
    }

    VkBuffer VertexBuffer::handle(size_t frameIndex) const
    {
        return _buffers[frameIndex]._buffer;
    }

    bool VertexBuffer::isValid(size_t frameIndex) const
    {
        return _buffers[frameIndex]._buffer != VK_NULL_HANDLE;
    }

}