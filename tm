#!/bin/bash

unset TMUX
tmux kill-session -t asio
tmux new -d -s asio
tmux switch -t asio
