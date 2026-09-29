#pragma once

struct SharedRenderTargetReadiness
{
    bool texture = false;
    bool surface = false;
    bool shared = false;

    bool Ready() const { return texture && surface && shared; }
};

struct RenderTargetReadiness
{
    SharedRenderTargetReadiness left;
    SharedRenderTargetReadiness right;
    SharedRenderTargetReadiness blank;

    bool Ready() const { return left.Ready() && right.Ready() && blank.Ready(); }
};
