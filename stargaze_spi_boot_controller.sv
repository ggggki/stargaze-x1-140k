//==============================================================================
// SPI BOOT FLASH CONTROLLER - Works with ALL Stargaze CPUs
// Reads RISC-V BIOS from motherboard SPI flash (Winbond W25Q128 or similar)
//==============================================================================

module stargaze_spi_boot_controller (
    input  logic        clk,           // System clock (100MHz)
    input  logic        rst_n,         // Active low reset
    input  logic        start_boot,    // CPU says "start booting"
    
    // SPI Interface (to motherboard flash chip)
    output logic        spi_cs_n,      // Chip select (active low)
    output logic        spi_sck,       // Serial clock (25MHz)
    output logic        spi_mosi,      // Master Out Slave In
    input  logic        spi_miso,      // Master In Slave Out
    
    // BIOS Output (to CPU)
    output logic [31:0] bios_data,     // 32-bit RISC-V instruction
    output logic        bios_valid,    // Data is valid
    output logic [31:0] bios_addr,     // Current read address
    
    // Status
    output logic        boot_loading,  // BIOS is being loaded
    output logic        boot_ready     // BIOS fully loaded, CPU can start
);

    //----------------------------------------------------------------------
    // SPI Flash Commands (Winbond W25Q128JV)
    //----------------------------------------------------------------------
    localparam CMD_READ      = 8'h03;   // Read data
    localparam CMD_FAST_READ = 8'h0B;   // Fast read (with dummy byte)
    localparam CMD_WAKE      = 8'hAB;   // Wake from power down
    localparam CMD_RDID      = 8'h9F;   // Read manufacturer ID
    
    // BIOS starts at address 0x000000 in flash (first 64KB)
    localparam BIOS_START_ADDR = 24'h000000;
    localparam BIOS_SIZE_BYTES = 24'h010000;  // 64KB BIOS
    
    //----------------------------------------------------------------------
    // SPI State Machine
    //----------------------------------------------------------------------
    typedef enum logic [3:0] {
        SPI_IDLE, SPI_WAKE, SPI_SEND_CMD,
        SPI_SEND_ADDR, SPI_READ_DATA, SPI_DONE
    } spi_state_t;
    
    spi_state_t state;
    
    // SPI clock divider: 100MHz → 25MHz (divide by 4)
    logic [1:0]  clk_div;
    logic        spi_clk_en;
    
    // Shift registers and counters
    logic [7:0]  shift_reg;       // Data to shift out
    logic [7:0]  data_reg;        // Data being received
    logic [3:0]  bit_count;       // Bits shifted (0-7)
    logic [2:0]  byte_count;      // Bytes received per instruction (0-3)
    logic [23:0] flash_addr;      // Current flash address
    
    //----------------------------------------------------------------------
    // Clock Divider
    //----------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            clk_div <= 2'b00;
            spi_clk_en <= 1'b0;
        end else begin
            clk_div <= clk_div + 1'b1;
            if (clk_div == 2'b11)
                spi_clk_en <= 1'b1;
            else
                spi_clk_en <= 1'b0;
        end
    end

    //----------------------------------------------------------------------
    // Main SPI Controller
    //----------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= SPI_IDLE;
            spi_cs_n <= 1'b1;
            spi_sck <= 1'b0;
            spi_mosi <= 1'b0;
            shift_reg <= 8'h00;
            data_reg <= 8'h00;
            bit_count <= 4'd0;
            byte_count <= 3'd0;
            flash_addr <= BIOS_START_ADDR;
            bios_data <= 32'h00000000;
            bios_valid <= 1'b0;
            bios_addr <= 32'd0;
            boot_loading <= 1'b0;
            boot_ready <= 1'b0;
        end else begin
            case (state)
                //------------------------------------------------------------------
                SPI_IDLE: begin
                    boot_ready <= 1'b0;
                    bios_valid <= 1'b0;
                    
                    if (start_boot) begin
                        boot_loading <= 1'b1;
                        flash_addr <= BIOS_START_ADDR;
                        state <= SPI_WAKE;
                        spi_cs_n <= 1'b0;     // Select flash chip
                        shift_reg <= CMD_WAKE;
                        bit_count <= 4'd0;
                    end
                end
                
                //------------------------------------------------------------------
                SPI_WAKE: begin
                    if (spi_clk_en) begin
                        spi_sck <= ~spi_sck;
                        if (spi_sck) begin
                            // Rising edge: output next bit
                            spi_mosi <= shift_reg[7];
                            shift_reg <= {shift_reg[6:0], 1'b0};
                            bit_count <= bit_count + 1'b1;
                            
                            if (bit_count == 4'd7) begin
                                spi_cs_n <= 1'b1;  // Deselect
                                bit_count <= 4'd0;
                                state <= SPI_SEND_CMD;
                            end
                        end
                    end
                end
                
                //------------------------------------------------------------------
                SPI_SEND_CMD: begin
                    // Send READ command (0x03)
                    spi_cs_n <= 1'b0;
                    shift_reg <= CMD_READ;
                    bit_count <= 4'd0;
                    byte_count <= 3'd0;
                    state <= SPI_SEND_ADDR;
                end
                
                //------------------------------------------------------------------
                SPI_SEND_ADDR: begin
                    if (spi_clk_en) begin
                        spi_sck <= ~spi_sck;
                        if (spi_sck) begin
                            spi_mosi <= shift_reg[7];
                            shift_reg <= {shift_reg[6:0], 1'b0};
                            bit_count <= bit_count + 1'b1;
                            
                            if (bit_count == 4'd7) begin
                                bit_count <= 4'd0;
                                
                                // Send 3 address bytes
                                case (byte_count)
                                    0: shift_reg <= flash_addr[23:16];
                                    1: shift_reg <= flash_addr[15:8];
                                    2: shift_reg <= flash_addr[7:0];
                                    default: begin
                                        byte_count <= 3'd0;
                                        state <= SPI_READ_DATA;
                                    end
                                endcase
                                byte_count <= byte_count + 1'b1;
                            end
                        end
                    end
                end
                
                //------------------------------------------------------------------
                SPI_READ_DATA: begin
                    if (spi_clk_en) begin
                        spi_sck <= ~spi_sck;
                        if (!spi_sck) begin
                            // Falling edge: sample data
                            data_reg <= {data_reg[6:0], spi_miso};
                            
                            if (byte_count == 3'd3) begin
                                // 4 bytes received = 1 RISC-V instruction
                                bios_data <= {data_reg[6:0], spi_miso, data_reg[15:0]};
                                bios_valid <= 1'b1;
                                bios_addr <= {8'd0, flash_addr};
                                
                                flash_addr <= flash_addr + 24'd4;
                                byte_count <= 3'd0;
                                
                                // Check if done
                                if (flash_addr >= (BIOS_START_ADDR + BIOS_SIZE_BYTES)) begin
                                    spi_cs_n <= 1'b1;
                                    boot_loading <= 1'b0;
                                    boot_ready <= 1'b1;
                                    state <= SPI_DONE;
                                end
                            end else begin
                                byte_count <= byte_count + 1'b1;
                            end
                        end else begin
                            // Rising edge: set MOSI to 0 (dummy data during read)
                            spi_mosi <= 1'b0;
                        end
                    end
                end
                
                //------------------------------------------------------------------
                SPI_DONE: begin
                    bios_valid <= 1'b0;
                    // Stay here until reset
                end
                
                default: state <= SPI_IDLE;
            endcase
        end
    end

endmodule