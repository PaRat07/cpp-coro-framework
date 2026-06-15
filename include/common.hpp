#pragma once

struct MoveOnly {
  MoveOnly() = default;

  MoveOnly(MoveOnly&&) = default;
  MoveOnly(const MoveOnly&) = delete;

  MoveOnly& operator=(MoveOnly&&) = default;
  MoveOnly& operator=(const MoveOnly&) = delete;
};

