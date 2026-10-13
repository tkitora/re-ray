# PC 上のユニットテスト(PlatformIO 不要)。  make test
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Iinclude
BUILD = build
SRCS = $(wildcard test/*.cpp)
HDRS = $(wildcard include/reray/*.h) $(wildcard test/*.h)

test: $(BUILD)/test_main
	./$(BUILD)/test_main

$(BUILD)/test_main: $(SRCS) $(HDRS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@

# 出口が MQTT に出す JSON が、JSON として正しいかを Python で確認する(test の後に実行)
json-check: test
	python3 test/check_json.py $(BUILD)/sample_json.txt

# ガードの確認: 次の3つは「コンパイルが通らない」のが正しい。
guard-test:
	@for d in "-DRERAY_MAX_TX_DBM=13" "-DRERAY_MAX_TX_DBM=10 -DRERAY_TX_DBM=11" "-DRERAY_TX_ENABLE=2"; do \
	  out=$$($(CXX) $(CXXFLAGS) $$d -fsyntax-only test/test_core.cpp 2>&1); \
	  if [ $$? -eq 0 ]; then echo "NG: $$d がビルドできてしまった"; exit 1; fi; \
	  if ! echo "$$out" | grep -q "static assertion failed"; then \
	    echo "NG: $$d は static_assert 以外の理由で失敗した"; echo "$$out"; exit 1; fi; \
	  echo "OK: $$d は static_assert で拒否された"; \
	done

clean:
	rm -rf $(BUILD)

.PHONY: test json-check guard-test clean
